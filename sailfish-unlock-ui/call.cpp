/* Copyright (c) 2019 Jolla Ltd. */

#include "call.h"
#include "logging.h"
#include <assert.h>
#include <functional>
#include <ohm-ext/route.h>
#include <res-conn.h>
#include <sailfish-mindbus/object.h>
#include <sailfish-minui-dbus/eventloop.h>
#include <string>

#define OFONO_SERVICE "org.ofono"
#define OFONO_MODEM_MANAGER_INTERFACE "org.nemomobile.ofono.ModemManager"
#define OFONO_MODEM_MANAGER_PATH "/"
#define OFONO_MODEM_INTERFACE "org.ofono.Modem"
#define OFONO_VOICECALL_MANAGER_INTERFACE "org.ofono.VoiceCallManager"
#define HIDE_CALLERID_DEFAULT ""
#define DEFAULT_EMERGENCY_NUMBER "112"

#define MSGTYPE(type) resmsg_type_str(type) << " (" << type << ")"

using namespace Sailfish;

Call::Call(MinUi::DBus::EventLoop *eventLoop)
    : m_eventLoop(eventLoop)
    , m_routeManager(nullptr)
    , m_voiceCallManager(nullptr)
    , m_systemBus(nullptr)
    , m_phoneNumber("")
    , m_statusCallback(nullptr)
    , m_callObjectPath("")
    , m_speakerEnabled(false)
    , m_ofonoStatus(OfonoIdle)
    , m_resourceStatus(ResourcesIdle)
    , m_rc(nullptr)
    , m_rset(nullptr)
    , m_reqno(0)
{
}

Call::~Call()
{
    endCall();
    if (m_resourceStatus == ResourceStatus::ResourcesAcquired)
        disconnectResources();
    if (m_voiceCallManager) {
        delete m_voiceCallManager;
        m_voiceCallManager = nullptr;
    }
    if (m_routeManager) {
        delete m_routeManager;
        m_routeManager = nullptr;
    }
    free(m_rc);
    m_rc = nullptr;
    free(m_rset);
    m_rset = nullptr;
}

void Call::makeCall(std::string &phoneNumber, Callback callback)
{
    assert(callback);

    m_statusCallback = callback;
    m_phoneNumber.assign(phoneNumber);
    // Initialize and connect to needed services
    if (!m_systemBus) {
        m_systemBus = MinDBus::systemBus();
        if (!m_systemBus) {
            log_err("System bus connect failed!");
            m_statusCallback(Error);
            return;
        }
    }
    if (m_ofonoStatus != OfonoIdle && m_ofonoStatus != OfonoError) {
        log_warning("Already calling");
        return;
    }
    startAcquiringResources();
    if (!m_routeManager) {
        m_routeManager = new MinDBus::Object(m_systemBus, OHM_EXT_ROUTE_MANAGER_INTERFACE, OHM_EXT_ROUTE_MANAGER_PATH, OHM_EXT_ROUTE_MANAGER_INTERFACE);
        m_routeManager->connect<const char *, uint32_t>("AudioRouteChanged", [this](const char *route, uint32_t) {
            if (strcmp(route, "earpiece") == 0) {
                m_speakerEnabled = false;
                m_statusCallback(EarpieceOn);
                log_debug("Audio routed to earpiece");
            } else if (strcmp(route, "speaker") == 0) {
                m_speakerEnabled = true;
                m_statusCallback(SpeakerOn);
                log_debug("Audio routed to speaker");
            }
        });
    }
    placeCall();
}

void Call::endCall()
{
    if (!calling())
        return;
    log_debug("Ending call");
    hangUp();
    releaseResources();
}

bool Call::calling()
{
    switch (m_ofonoStatus) {
    case OfonoInitializing:
    case OfonoReady:
    case OfonoCalling:
        return true;
    default:
        return false;
    }
}

void Call::enableSpeaker()
{
    if (m_routeManager) {
        m_routeManager->call(OHM_EXT_ROUTE_ENABLE_METHOD, "speaker")->onError(
            [this](const char *name, const char *message) {
                log_err("Error while enabling speaker: " << name << ": " << message);
            });
    }
}

void Call::disableSpeaker()
{
    if (m_routeManager) {
        m_routeManager->call(OHM_EXT_ROUTE_DISABLE_METHOD, "speaker")->onError(
            [this](const char *name, const char *message) {
                log_err("Error while disabling speaker: " << name << ": " << message);
            });
    }
}

bool Call::speakerEnabled()
{
    return m_speakerEnabled;
}

void Call::toggleSpeaker()
{
    if (speakerEnabled()) {
        disableSpeaker();
    } else {
        enableSpeaker();
    }
}

void Call::placeCall()
{
    m_ofonoStatus = OfonoInitializing;
    // Place the emergency call with ofono
    log_debug("Placing call with ofono");
    if (m_voiceCallManager) {
        bringModemOnline();
        return;
    }
    // MinDBus doesn't handle container types
    DBusMessage *message = dbus_message_new_method_call(OFONO_SERVICE,
            OFONO_MODEM_MANAGER_PATH, OFONO_MODEM_MANAGER_INTERFACE, "GetAvailableModems");
    DBusPendingCall *call = nullptr;
    if (!dbus_connection_send_with_reply(m_systemBus, message, &call, -1) || !call ||
            !setPendingCallNotify(call, std::mem_fn(&Call::handleModemPath))) {
        log_err("Could not read available modems");
        m_ofonoStatus = OfonoError;
        m_statusCallback(Error);
    }
    if (call)
        dbus_pending_call_unref(call);
    dbus_message_unref(message);
}

void Call::handleModemPath(DBusPendingCall *call)
{
    DBusMessage *message = dbus_pending_call_steal_reply(call);
    DBusError error = DBUS_ERROR_INIT;
    if (dbus_set_error_from_message(&error, message)) {
        log_err("Could not fetch modem object path. Can not call!");
        log_err(error.name << ": " << error.message);
        m_ofonoStatus = OfonoError;
        m_statusCallback(Error);

    } else {
        const char **objectArray = 0;
        int objectCount = 0;
        if (!dbus_message_get_args(message, &error, DBUS_TYPE_ARRAY, DBUS_TYPE_OBJECT_PATH,
                    &objectArray, &objectCount, DBUS_TYPE_INVALID)) {
            log_err("Invalid arguments for modem path. Can not call!");
            log_err(error.name << ": " << error.message);

        } else {
            if (m_voiceCallManager)  // This should not be called twice but just in case that happens
                delete m_voiceCallManager;
            m_voiceCallManager = new MinDBus::Object(m_systemBus, OFONO_SERVICE, objectArray[0], OFONO_VOICECALL_MANAGER_INTERFACE);

            m_voiceCallManager->connect<MinDBus::ObjectPath>("CallRemoved",
                [this](MinDBus::ObjectPath call) {
                    if (calling() && m_callObjectPath.compare(call) == 0) {
                        log_warning("The other end ended the call or there was an error");
                        releaseResources();
                        m_callObjectPath.assign("");
                        disableEmergencyCallMode();
                        m_ofonoStatus = OfonoIdle;
                        m_statusCallback(Ended);
                    } else {
                        log_debug("Got unhandled call removal for " << call);
                    }
            });

            bringModemOnline();
        }
    }

    dbus_error_free(&error);
    dbus_message_unref(message);
}

void Call::bringModemOnline()
{
    log_debug("Setting modem online");

    // MinDBus doesn't handle variant type
    DBusMessage *message = dbus_message_new_method_call(OFONO_SERVICE,
            m_voiceCallManager->path(), OFONO_MODEM_INTERFACE, "SetProperty");

    DBusMessageIter iter, subiter;
    const char *propertyName = "Online";
    const int modemState = true;
    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &subiter);
    dbus_message_iter_append_basic(&subiter, DBUS_TYPE_BOOLEAN, &modemState);
    dbus_message_iter_close_container(&iter, &subiter);

    DBusPendingCall *call = nullptr;
    if (!dbus_connection_send_with_reply(m_systemBus, message, &call, -1) || !call ||
            !setPendingCallNotify(call, std::mem_fn(&Call::handleModemOnline))) {
        log_err("Could not set modem online! Can not call!");
        m_ofonoStatus = OfonoError;
        m_statusCallback(Error);
    }
    if (call)
        dbus_pending_call_unref(call);
    dbus_message_unref(message);
}

void Call::handleModemOnline(DBusPendingCall *call)
{
    DBusMessage *message = dbus_pending_call_steal_reply(call);
    DBusError error = DBUS_ERROR_INIT;
    if (dbus_set_error_from_message(&error, message)) {
        log_err("Could not set modem online! Can not call!");
        log_err(error.name << ": " << error.message);
        m_ofonoStatus = OfonoError;
        m_statusCallback(Error);
    } else {
        log_debug("Modem online!");
        checkForNonEmergencyNumber();
    }
    dbus_error_free(&error);
    dbus_message_unref(message);
}

void Call::checkForNonEmergencyNumber() {
    // MinDBus doesn't handle array, dict entry or variant type
    DBusMessage *message = dbus_message_new_method_call(OFONO_SERVICE,
            m_voiceCallManager->path(), m_voiceCallManager->interface(), "GetProperties");
    DBusPendingCall *call = nullptr;
    if (!dbus_connection_send_with_reply(m_systemBus, message, &call, -1) || !call ||
            !setPendingCallNotify(call, std::mem_fn(&Call::handleEmergencyNumbersProperty))) {
        log_err("Could not read emergency numbers. Not checking the number.");
        if (m_ofonoStatus == OfonoInitializing) {
            m_ofonoStatus = OfonoReady;
            dial();
        }
    }
    if (call)
        dbus_pending_call_unref(call);
    dbus_message_unref(message);
}

void Call::handleEmergencyNumbersProperty(DBusPendingCall *call) {
    bool is_ok = true;

    if (!m_phoneNumber.empty()) {
        DBusMessage *message = dbus_pending_call_steal_reply(call);
        DBusError error = DBUS_ERROR_INIT;
        DBusMessageIter iter;
        if (dbus_set_error_from_message(&error, message) ||
                !dbus_message_iter_init(message, &iter) ||
                strcmp(dbus_message_iter_get_signature(&iter), "a{sv}") != 0) {
            log_err("Could not read emergency numbers. Not checking the number.");
            if (dbus_error_is_set(&error))
                log_err(error.name << ": " << error.message);

        } else {
            DBusMessageIter arrayIter;
            dbus_message_iter_recurse(&iter, &arrayIter);
            DBusMessageIter dictIter;
            const char *name = nullptr;
            while (dbus_message_iter_get_arg_type(&arrayIter) != DBUS_TYPE_INVALID) {
                // Check the contained dict
                dbus_message_iter_recurse(&arrayIter, &dictIter);
                dbus_message_iter_get_basic(&dictIter, &name);
                if (strcmp(name, "EmergencyNumbers") == 0)
                    break;
                dbus_message_iter_next(&arrayIter);
            }

            if (!name || strcmp(name, "EmergencyNumbers") != 0) {
                log_err("List of emergency numbers was not found! Not checking the number.");
            } else {
                // Found the correct dict, read the variant
                if (!dbus_message_iter_next(&dictIter)) {
                    log_err("Could not read variant! Not checking the number.");
                } else {
                    // Open the variant and check the numbers
                    DBusMessageIter variant;
                    dbus_message_iter_recurse(&dictIter, &variant);
                    if (strcmp(dbus_message_iter_get_signature(&variant), "as") != 0) {
                        log_err("Wrong type, expected array! Not checking the number.");
                    } else if (!checkForEmergencyNumber(variant)) {
                        m_ofonoStatus = OfonoIdle;
                        m_statusCallback(InvalidNumber);
                        is_ok = false;
                        log_debug("Tried to call to non-emergency number. Prevented.");
                    }
                }
            }
        }

        dbus_error_free(&error);
        dbus_message_unref(message);
    } else {
        log_debug("Empty phone number given, calling default " << DEFAULT_EMERGENCY_NUMBER);
    }

    if (is_ok && m_ofonoStatus == OfonoInitializing) {
        m_ofonoStatus = OfonoReady;
        dial();
    }
}

bool Call::checkForEmergencyNumber(DBusMessageIter &variant)
{
    DBusMessageIter arrayIter;
    dbus_message_iter_recurse(&variant, &arrayIter);
    while (dbus_message_iter_get_arg_type(&arrayIter) != DBUS_TYPE_INVALID) {
        const char *number;
        dbus_message_iter_get_basic(&arrayIter, &number);
        if (m_phoneNumber.compare(number) == 0)
            return true;
        dbus_message_iter_next(&arrayIter);
    }
    return false;
}

void Call::dial()
{
    if (m_ofonoStatus != OfonoReady) {
        // Wait until ofono is ready
        log_debug("Dial called but ofono was not yet ready to call.");
        return;
    }

    switch (m_resourceStatus) {
    case ResourcesConnecting:
    case ResourcesAcquiring:
        // Wait until resources are ready or have failed.
        break;
    case ResourcesAcquired: {
        enableEmergencyCallMode();
        m_ofonoStatus = OfonoCalling;
        auto call = m_voiceCallManager->call<MinDBus::ObjectPath>("Dial",
                m_phoneNumber.empty() ? DEFAULT_EMERGENCY_NUMBER : m_phoneNumber.c_str(), HIDE_CALLERID_DEFAULT);
        call->onFinished([this](MinDBus::ObjectPath call) {
            log_debug("Dialing " << m_phoneNumber << ", call " << call);
            m_callObjectPath.assign(call);
            m_statusCallback(Calling);
        });
        call->onError([this](const char *name, const char *message) {
            log_err("Error while dialing: " << name << ": " << message);
            disableEmergencyCallMode();
            m_ofonoStatus = OfonoError;
            m_statusCallback(Error);
        });
        break;
    }
    case ResourcesError:
        log_debug("Resources were not acquired. Not calling!");
        break;
    default:
        // Any other statuses should not be possible here
        log_err("Resources should have been acquired by now!");
        break;
    }
}

void Call::hangUp()
{
    // Tell ofono to hang up
    disableEmergencyCallMode();
    if (m_ofonoStatus != OfonoCalling) {
        m_ofonoStatus = OfonoIdle;
        return;
    }
    auto call = m_voiceCallManager->call("HangupAll");
    call->onFinished([this] {
        log_debug("Ofono ended call");
        m_callObjectPath.assign("");
        m_ofonoStatus = OfonoIdle;
        m_statusCallback(Ended);
    });
    call->onError([this](const char *name, const char *message) {
        log_err("While ending call: " << name << ": " << message);
        m_callObjectPath.assign("");
        m_ofonoStatus = OfonoError;
        m_statusCallback(Error);
    });
}

bool Call::setPendingCallNotify(DBusPendingCall *call, CallbackMethod method)
{
    auto cb = new CallbackHandle{this, method};
    return dbus_pending_call_set_notify(call, handleCallback, cb, free);
}

void Call::handleCallback(DBusPendingCall *call, void *data)
{
    auto cb = static_cast<CallbackHandle *>(data);
    cb->method(cb->instance, call);
}

void Call::startAcquiringResources()
{
    // Acquire "call" class with "AudioPlayback" and "AudioRecording" resources
    if (m_rc) {
        log_debug("Already connected, acquiring resources next");
        acquiringResources();

    } else {
        m_rc = resproto_init(RESPROTO_ROLE_CLIENT, RESPROTO_TRANSPORT_DBUS, NULL, m_systemBus);
        if (!m_rc) {
            log_debug("Resproto init failed");
            m_resourceStatus = ResourcesError;
            m_statusCallback(Error);
        } else {
            connectResources();
        }
    }
}

void Call::releaseResources()
{
    // End call and release "call" class
    resmsg_t msg;

    if (m_rset == nullptr || m_resourceStatus == ResourcesReleasing)
        return;

    m_resourceStatus = ResourcesReleasing;

    msg.possess.type = RESMSG_RELEASE;
    msg.possess.id = 1;
    msg.possess.reqno = ++m_reqno;
    resproto_send_message(m_rset, &msg, resourceStatusHandler);
}

void Call::connectResources()
{
    if (m_rset)
        return;  // Already connected

    log_debug("Connecting to resource manager");

    resmsg_t msg;
    msg.record.type = RESMSG_REGISTER;
    msg.record.id = 1;
    msg.record.reqno = ++m_reqno;
    msg.record.rset.all = RESMSG_AUDIO_PLAYBACK | RESMSG_AUDIO_RECORDING;
    msg.record.rset.opt = 0;
    msg.record.rset.share = 0;
    msg.record.rset.mask = 0;
    msg.record.klass = (char *)"call";
    msg.record.mode = RESMSG_MODE_AUTO_RELEASE;

    m_resourceStatus = ResourcesConnecting;
    m_rset = resconn_connect(m_rc, &msg, resourceStatusHandler);
    m_rset->userdata = this;
}

void Call::acquiringResources()
{
    resmsg_t msg;

    log_debug("Acquiring resources");

    msg.possess.type = RESMSG_ACQUIRE;
    msg.possess.id = 1;
    msg.possess.reqno = ++m_reqno;

    int success = resproto_send_message(m_rset, &msg, resourceStatusHandler);
    if (!success) {
        log_err("Could not acquire resources");
        m_resourceStatus = ResourcesError;
        m_statusCallback(Error);
    } else {
        m_resourceStatus = ResourcesAcquired;
        dial();
    }
}

void Call::resourceStatusHandler(resset_t *rset, resmsg_t *msg)
{
    auto instance = static_cast<Call *>(rset->userdata);
    if (msg->type == RESMSG_STATUS)
        log_debug("Resource status: " << msg->status.errmsg << " (" << msg->status.errcod << ")");
    else
        log_err("Resource status message of wrong type: " << MSGTYPE(msg->type));

    if (msg->type == RESMSG_STATUS && msg->status.errcod == 0) {
        if (instance->m_resourceStatus == ResourcesConnecting) {
            instance->m_resourceStatus = ResourcesAcquiring;
            instance->acquiringResources();
        } else if (instance->m_resourceStatus == ResourcesReleasing) {
            instance->m_resourceStatus = ResourcesIdle;
        } else if (instance->m_resourceStatus == ResourcesDisconnecting) {
            instance->m_resourceStatus = ResourcesIdle;
            instance->m_rset = nullptr;
            instance->m_rc = nullptr;
        }
    }
}

void Call::disconnectResources()
{
    log_debug("Disconneting from resource manager");

    resmsg_t msg;
    msg.possess.type = RESMSG_UNREGISTER;
    msg.possess.id = 1;
    msg.possess.reqno = ++m_reqno;

    m_resourceStatus = ResourcesDisconnecting;
    resconn_disconnect(m_rset, &msg, resourceStatusHandler);
}

void Call::enableEmergencyCallMode()
{
    // Enable emergency call in ohm, adjusts volume to maximum
    if (m_routeManager) {
        auto call = m_routeManager->call(OHM_EXT_ROUTE_ENABLE_METHOD, "emergencycall");
        call->onFinished([this] {
            log_debug("Emergency call mode enabled");
        });
        call->onError([this](const char *name, const char *message) {
            log_err("Error while enabling emergency call mode: " << name << ": " << message);
        });
    }
}

void Call::disableEmergencyCallMode()
{
    if (m_routeManager) {
        auto call = m_routeManager->call(OHM_EXT_ROUTE_DISABLE_METHOD, "emergencycall");
        call->onFinished([this] {
            log_debug("Emergency call mode disabled");
        });
        call->onError([this](const char *name, const char *message) {
            log_err("Error while disabling emergency call mode: " << name << ": " << message);
        });
    }
}
