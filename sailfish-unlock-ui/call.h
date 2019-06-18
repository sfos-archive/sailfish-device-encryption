/* Copyright (c) 2019 Jolla Ltd. */

#ifndef UNLOCK_AGENT_CALL_H_
#define UNLOCK_AGENT_CALL_H_
#include <functional>
#include <res-conn.h>
#include <sailfish-minui-dbus/eventloop.h>
#include <sailfish-mindbus/object.h>
#include <string>

namespace Sailfish {

class Call
{
public:
    Call(MinUi::DBus::EventLoop *eventLoop);
    ~Call();

    enum Status {
        Calling,
        Error,
        InvalidNumber,
        Ended,
    };

    typedef std::function<void(Status status)> Callback;

    /* Make call, arguments are phone number to call and callback to signal Status */
    void makeCall(std::string &phoneNumber, Callback callback);

    /* Hang up call */
    void endCall();

    /* To check if we are calling or not */
    bool calling();

    /* Enable speaker instead of ear piece */
    void enableSpeaker();

    /* Disable speaker and enable ear piece instead */
    void disableSpeaker();

    /* Check if speaker is enabled */
    bool speakerEnabled();

    /* Enable or disable speaker depending on if it is enabled */
    void toggleSpeaker();

private:

    void placeCall();
    void handleModemPath(DBusPendingCall *call);
    void bringModemOnline();
    void handleModemOnline(DBusPendingCall *call);
    void checkForNonEmergencyNumber();
    void handleEmergencyNumbersProperty(DBusPendingCall *call);
    bool checkForEmergencyNumber(DBusMessageIter &variant);
    void dial();
    void hangUp();

    typedef decltype(std::mem_fn(&Call::handleModemPath)) CallbackMethod;
    typedef struct {
        Call *instance;
        CallbackMethod method;
    } CallbackHandle;
    bool setPendingCallNotify(DBusPendingCall *call, CallbackMethod method);
    static void handleCallback(DBusPendingCall *call, void *data);

    void startAcquiringResources();
    void releaseResources();
    static void resourceStatusHandler(resset_t *rset, resmsg_t *msg);
    void connectResources();
    void acquiringResources();
    void disconnectResources();

    void enableEmergencyCallMode();
    void disableEmergencyCallMode();

    MinUi::DBus::EventLoop *m_eventLoop;
    MinDBus::Object *m_routeManager;
    MinDBus::Object *m_voiceCallManager;
    DBusConnection *m_systemBus;
    std::string m_phoneNumber;
    Callback m_statusCallback;
    char *m_callObjectPath;
    bool m_speakerEnabled;

    enum OfonoStatus {
        OfonoIdle,
        OfonoInitializing,
        OfonoReady,
        OfonoCalling,
        OfonoError
    };

    OfonoStatus m_ofono_status;

    enum ResourceStatus {
        ResourcesIdle,
        ResourcesConnecting,
        ResourcesAcquiring,
        ResourcesAcquired,
        ResourcesReleasing,
        ResourcesDisconnecting,
        ResourcesError
    };

    ResourceStatus m_resource_status;

    resconn_t *m_rc;
    resset_t *m_rset;
    uint32_t m_reqno;
};

}

#endif /* UNLOCK_AGENT_CALL_H_ */
