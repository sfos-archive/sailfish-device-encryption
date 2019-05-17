/*
 * Copyright (C) 2019 Jolla Ltd
 */

#ifndef UNLOCK_AGENT_COMPOSITOR_H_
#define UNLOCK_AGENT_COMPOSITOR_H_
#include <dbus/dbus.h>
#include <sailfish-minui/ui.h>

namespace Sailfish {

class Compositor
{
public:
    enum DisplayState {
        Unknown = -1,
        Off,
        Dim,
        On,
    };

    DisplayState displayState() {
        return m_displayState;
    }

    bool updatesEnabled() {
        return m_updatesEnabled > 0;
    }

    Compositor(MinUi::EventLoop *eventLoop);
    virtual ~Compositor();

    virtual void displayStateChanged() {
    }

    virtual void updatesEnabledChanged() {
    }

    void setBlankPreventWanted(bool wanted);

private:
    typedef DBusMessage *(Compositor::*HandlerCallback)(DBusMessage *msg);

    struct Handler
    {
        Compositor::HandlerCallback callback;
        const char *service;
        const char *object;
        const char *interface;
        const char *member;
        const char *arg0;
        const char *arg1;
        const char *arg2;
    };

    void updateMceAvailable(bool available);
    void updateMceNameOwner(const char *owner);
    DBusMessage *mceNameOwnerHandler(DBusMessage *msg);
    static void mceNameOwnerReply(DBusPendingCall *pc, void *aptr);
    void mceNameOwnerQuery();

    void updateDisplayState(const char *state);
    DBusMessage *displayStateHandler(DBusMessage *msg);
    static void displayStateReply(DBusPendingCall *pc, void *aptr);
    void displayStateQuery();

    void terminateBlankingPause();
    void requestBlankingPause();
    void evaluateBlankingPause();
    void updateBlankPreventAllowed(bool allowed);
    DBusMessage *blankPreventAllowedHandler(DBusMessage *msg);
    static void blankPreventAllowedReply(DBusPendingCall *pc, void *aptr);
    void blankPreventAllowedQuery();

    void updateUpdatesEnabled(bool state);
    DBusMessage *updatesEnabledHandler(DBusMessage *msg);

    static DBusHandlerResult systemBusFilter(DBusConnection *con, DBusMessage *msg, void *aptr);
    bool generateMatch(Compositor::Handler &handler, char *buff, size_t size);
    void addSystemBusMatches();
    void removeSystemBusMatches();
    void connectToSystemBus();
    void disconnectFromSystemBus();

private:
    DBusConnection *m_systemBus;
    DisplayState    m_displayState;
    int             m_updatesEnabled;

    bool            m_mceAvailable;
    char           *m_mceNameOwner;

    bool            m_blankPreventWanted;
    bool            m_blankPreventAllowed;
    int             m_blankPreventTimer;
    MinUi::EventLoop *m_eventLoop;

    static Handler s_systemBusHandlers[];

};
}
#endif /* UNLOCK_AGENT_COMPOSITOR_H_ */
