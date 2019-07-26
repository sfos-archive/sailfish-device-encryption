#include <QtGlobal>
#include <QtQml>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QTranslator>

#include "encryptionstatus.h"

// using custom translator so it gets properly removed from qApp when engine is deleted
class AppTranslator: public QTranslator
{
    Q_OBJECT
public:
    AppTranslator(QObject *parent)
        : QTranslator(parent)
    {
        qApp->installTranslator(this);
    }

    virtual ~AppTranslator()
    {
        qApp->removeTranslator(this);
    }
};


class SailfishEncryptionPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Sailfish.Encryption")

public:
    void initializeEngine(QQmlEngine *engine, const char *uri)
    {
        Q_UNUSED(uri)

        AppTranslator *engineeringEnglish = new AppTranslator(engine);
        engineeringEnglish->load("settings-encryption_eng_en", "/usr/share/translations");

        AppTranslator *translator = new AppTranslator(engine);
        translator->load(QLocale(), "settings-encryption", "-", "/usr/share/translations");
    }

    void registerTypes(const char *uri)
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("Sailfish.Encryption"));

        qmlRegisterUncreatableType<EncryptionStatus>("Sailfish.Encryption", 1, 0, "EncryptionStatus", "");
    }
};

#include "plugin.moc"
