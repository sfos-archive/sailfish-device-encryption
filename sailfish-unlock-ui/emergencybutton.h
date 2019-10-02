/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include <sailfish-minui/image.h>

#ifndef UNLOCK_AGENT_EMERGENCY_BUTTON
#define UNLOCK_AGENT_EMERGENCY_BUTTON

using namespace Sailfish::MinUi;

class EmergencyButton : public ActivatableItem
{
public:
    explicit EmergencyButton(const char *normal, const char *pressed, Item *parent = nullptr);
    ~EmergencyButton();

protected:
    void layout() override;
    void updateState(bool enabled) override;

    Image m_normal;
    Image m_pressed;
};

#endif
