/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "copyhelper.h"
#include <QFileInfo>
#include <QDebug>

static const int success = 0;
const QString homeCopyServicePath = QStringLiteral("/usr/libexec/sailfish-home-copy-service");

CopyHelper::CopyHelper(QObject *parent)
    : QObject(parent)
{
    connect(&m_partitionManager, &PartitionManager::externalStoragesPopulated,
            this, &CopyHelper::externalStoragesPopulated);
}

bool CopyHelper::checkWritable(const QString &path) const
{
    return QFileInfo(path).isWritable();
}

bool CopyHelper::hasHomeCopyService() const
{
    return QFileInfo(homeCopyServicePath).exists();
}

bool CopyHelper::memorycard() const
{
    auto partitions = m_partitionManager.partitions(Partition::External | Partition::ExcludeParents);
    return partitions.size() > 0;
}

void CopyHelper::externalStoragesPopulated()
{
    auto partitions = m_partitionManager.partitions(Partition::External | Partition::ExcludeParents);
    emit memorycardChanged(partitions.size() > 0);
}

qint64 CopyHelper::homeBytes() const
{
    auto partitions = m_partitionManager.partitions(Partition::User | Partition::ExcludeParents);
    if (partitions.size() == 1) {
        return partitions.first().bytesTotal() - partitions.first().bytesFree();
    } else {
        qWarning() << "Home partition not found, or multiple user partitions found";
        return LLONG_MAX;
    }
}
