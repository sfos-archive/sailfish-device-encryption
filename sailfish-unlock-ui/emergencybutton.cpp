/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "emergencybutton.h"

EmergencyButton::EmergencyButton(const char *normal, const char *pressed, Item *parent)
    : ActivatableItem(parent)
    , m_normal(normal, this)
    , m_pressed(pressed, this)
{
    resize(m_normal.width() + (2 * theme.paddingMedium), m_normal.height() + (2 * theme.paddingMedium));
    m_pressed.setVisible(false);
}

EmergencyButton::~EmergencyButton()
{

}

void EmergencyButton::layout()
{
    m_normal.centerIn(*this);
    m_pressed.centerIn(*this);
}

void EmergencyButton::updateState(bool enabled)
{
    (void)enabled;
    if (isPressed()) {
        m_normal.setVisible(false);
        m_pressed.setVisible(true);
    } else {
        m_normal.setVisible(true);
        m_pressed.setVisible(false);
    }
}
