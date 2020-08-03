/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef COPY_HELPER_H
#define COPY_HELPER_H

#include <QObject>
#include <QString>
#include <QDebug>
#include <systemsettings/partitionmanager.h>

class CopyHelper : public QObject
{
    Q_OBJECT

public:
    explicit CopyHelper(QObject *parent = nullptr);
    Q_INVOKABLE bool checkWritable(const QString &path) const;
    Q_INVOKABLE bool hasHomeCopyService() const;
    Q_INVOKABLE qint64 homeBytes() const;
    Q_PROPERTY(bool memorycard READ memorycard NOTIFY memorycardChanged)

    bool memorycard() const;

signals:
    void memorycardChanged(bool val);

public slots:
    void setMemorycard(bool exists);
    void externalStoragesPopulated();

private:
    PartitionManager m_partitionManager;
    qint64 m_homeBytes;
};
#endif //COPY_HELPER_H
