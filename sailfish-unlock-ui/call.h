/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
**
** All rights reserved.
**
** This file is part of Sailfish Device Encryption package.
**
** You may use this file under the terms of BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************************/

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
        Initializing,   // Ofono initialization ongoing
        Calling,        // Call ongoing, may or may not be active
        Error,          // Some error happened, not calling
        InvalidNumber,  // Invalid number, call was not started
        Ended,          // Call was ended
        EarpieceOn,     // Sound routed to earpiece, speaker off
        SpeakerOn,      // Sound routed to speaker, earpiece off
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
    MinDBus::Connection m_systemBus;
    std::string m_phoneNumber;
    Callback m_statusCallback;
    std::string m_callObjectPath;
    bool m_speakerEnabled;

    enum OfonoStatus {
        OfonoIdle,
        OfonoInitializing,
        OfonoReady,
        OfonoCalling,
        OfonoError
    };

    OfonoStatus m_ofonoStatus;

    enum ResourceStatus {
        ResourcesIdle,
        ResourcesConnecting,
        ResourcesAcquiring,
        ResourcesAcquired,
        ResourcesReleasing,
        ResourcesDisconnecting,
        ResourcesError
    };

    ResourceStatus m_resourceStatus;

    resconn_t *m_rc;
    resset_t *m_rset;
    uint32_t m_reqno;
};

}

#endif /* UNLOCK_AGENT_CALL_H_ */
