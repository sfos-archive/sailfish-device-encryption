/*
 * Copyright (c) 2019 Open Mobile Platform LLC.
 *
 * License: Proprietary
 */

#include "homeinfo.h"

#include <QDebug>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <systemsettings/udisks2defines.h>
#include <systemsettings/udisks2block_p.h>

static const auto luks = QStringLiteral("LUKS");
static const auto home = QStringLiteral("/dev/sailfish/home");

HomeInfo::HomeInfo(QObject *parent)
    : QObject(parent)
    , m_homeBlock(nullptr)
    , m_loading(true)
    , m_blockWaitCount(0)
{
    QDBusInterface managerInterface(UDISKS2_SERVICE,
                                    UDISKS2_MANAGER_PATH,
                                    UDISKS2_MANAGER_INTERFACE,
                                    QDBusConnection::systemBus());

    QVariantMap devSpec;
    devSpec.insert(QStringLiteral("path"), home);

    QDBusPendingCall pendingCall = managerInterface.asyncCallWithArgumentList(
                QStringLiteral("ResolveDevice"),
                QVariantList() << devSpec << QVariantMap());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            QDBusPendingReply<QList<QDBusObjectPath> > reply = *watcher;
            const QList<QDBusObjectPath> blockDevicePaths = reply.argumentAt<0>();
            m_blockWaitCount = blockDevicePaths.count();
            for (const QDBusObjectPath &dbusObjectPath : blockDevicePaths) {
                UDisks2::Block *block = new UDisks2::Block(dbusObjectPath.path(), UDisks2::InterfacePropertyMap());
                connect(block, &UDisks2::Block::completed, this, [this]() {
                    UDisks2::Block *completedBlock = qobject_cast<UDisks2::Block *>(sender());
                    completedBlock->dumpInfo();

                    if (!m_homeBlock && completedBlock->symlinks().contains(home)) {
                        m_homeBlock = completedBlock;
                        m_loading = false;

                        emit typeChanged();
                        emit versionChanged();
                        emit readonlyChanged();
                        emit sizeChanged();
                        emit loadingChanged();
                    } else {
                        completedBlock->deleteLater();
                    }
                    --m_blockWaitCount;
                    if (m_blockWaitCount == 0 && m_loading) {
                        m_loading = false;
                        emit loadingChanged();
                    }
                });
            }
        } else if (watcher->isError()) {
            m_loading = false;
            emit loadingChanged();
        }
    });
}

HomeInfo::~HomeInfo()
{
    m_homeBlock->deleteLater();
    m_homeBlock.clear();
}

QString HomeInfo::version() const
{
    if (!m_homeBlock)
        return QString();

    return m_homeBlock->idVersion();
}

QString HomeInfo::type() const
{
    if (!m_homeBlock)
        return QString();

    return m_homeBlock->idType().contains(luks) ? luks : QString();
}

bool HomeInfo::readonly() const
{
    if (!m_homeBlock)
        return false;

    return m_homeBlock->isReadOnly();
}

qint64 HomeInfo::size() const
{
    if (!m_homeBlock)
        return 0;

    return m_homeBlock->size();

}

bool HomeInfo::loading() const
{
    return m_loading;
}
