/*
 * Copyright (c) 2019 Open Mobile Platform LLC.
 *
 * License: Proprietary
 */

#ifndef ENCRYPTION_INFO_H
#define ENCRYPTION_INFO_H

#include <QObject>
#include <QString>

#include <systemsettings/udisks2block_p.h>

class HomeInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString type READ type NOTIFY typeChanged FINAL)
    Q_PROPERTY(QString version READ version NOTIFY versionChanged FINAL)
    Q_PROPERTY(bool readonly READ readonly NOTIFY readonlyChanged FINAL)
    Q_PROPERTY(qint64 size READ size NOTIFY sizeChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)

public:
    HomeInfo(QObject *parent = nullptr);
    ~HomeInfo();

    QString type() const;
    QString version() const;
    bool readonly() const;
    qint64 size() const;
    bool loading() const;

signals:
    void typeChanged();
    void versionChanged();
    void readonlyChanged();
    void sizeChanged();
    void loadingChanged();

private:
    QPointer<UDisks2::Block> m_homeBlock;
    bool m_loading;
    int m_blockWaitCount;
};

#endif // ENCRYPTION_STATUS_H
