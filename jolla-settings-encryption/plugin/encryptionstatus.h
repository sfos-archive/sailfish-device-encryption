/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef ENCRYPTION_STATUS_H
#define ENCRYPTION_STATUS_H

#include <QObject>

class EncryptionStatus : public QObject
{
    Q_OBJECT
public:
    enum Status {
        Idle,
        Busy,
        Encrypted,
        Error
    };
    Q_ENUM(Status)
};

#endif // ENCRYPTION_STATUS_H
