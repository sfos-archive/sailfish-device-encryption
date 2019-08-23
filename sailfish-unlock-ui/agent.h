/*
 * Copyright (C) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef UNLOCK_AGENT_AGENT_H_
#define UNLOCK_AGENT_AGENT_H_
#include <sailfish-minui-dbus/eventloop.h>

#include <glib.h>
#include "call.h"
#include "compositor.h"
#include "pin.h"

namespace Sailfish {

class Agent : public Compositor
{
public:
    Agent();
    ~Agent();
    static bool checkIfAgentCanRun();
    int execute(bool uiDebugging = false);
    bool sendPassword(const std::string& password);

private:
    Agent(MinUi::DBus::EventLoop *eventLoop);

    void startWatchingForAskFiles();
    bool handleNewAskFile(int descriptor, uint32_t events);
    inline bool checkForTemporaryPassphrase();
    static inline bool temporaryPassphraseIsSet();
    bool findAskFile();
    static int isAskFile(const struct dirent *ep);
    inline bool parseAskFile(const char *askFile);
    inline bool parseGKeyFile(GKeyFile *key_file);
    static inline bool timeInPast(long time);
    void tryToCreateUI();

    virtual void displayStateChanged();
    virtual void chargerStateChanged();
    virtual void batteryStatusChanged();
    virtual void batteryLevelChanged();
    virtual void updatesEnabledChanged();
    virtual void dsmeStateChanged();
    virtual void targetUnitActiveChanged();

    MinUi::DBus::EventLoop *m_eventLoop;
    PinUi *m_ui;

    bool m_uiAllowed;
    bool m_uiDebugging;

    std::string m_askFile;
    std::string m_socket;
    int m_pid;
    long m_notAfter;
};
}
#endif /* UNLOCK_AGENT_AGENT_H_ */
