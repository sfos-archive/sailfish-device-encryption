/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

#include <QGuiApplication>
#include <QQuickView>
#include <QtQml>

Q_DECL_EXPORT int main(int argc, char **argv)
{
    QScopedPointer<QTranslator> engineeringEnglish(new QTranslator);
    engineeringEnglish->load("settings-encryption_eng_en", "/usr/share/translations");
    QScopedPointer<QTranslator> translator(new QTranslator);
    translator->load(QLocale(), "settings-encryption", "-", "/usr/share/translations");

    QScopedPointer<QGuiApplication> a(new QGuiApplication(argc, argv));
    QScopedPointer<QQuickView> view(new QQuickView);

    qApp->installTranslator(engineeringEnglish.data());
    qApp->installTranslator(translator.data());

    QString path;
    path = qApp->applicationDirPath() + QDir::separator();

    QObject::connect(view->engine(), SIGNAL(quit()), qApp, SLOT(quit()));

    view->setSource(QUrl::fromLocalFile("/usr/share/sailfish-home-restoration/main.qml"));
    view->setFlags(view->flags() | Qt::WindowOverridesSystemGestures);
    view->showFullScreen();

    return a->exec();
}

