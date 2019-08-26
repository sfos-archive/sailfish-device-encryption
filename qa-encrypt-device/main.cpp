/**
 * Copyright (c) 2019 Open Mobile Platform LLC.
 *
 * License: Proprietary
 */

#include <QCoreApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QQmlApplicationEngine engine(QString(DEPLOYMENT_PATH) + QLatin1String("main.qml"));
    return app.exec();
}
