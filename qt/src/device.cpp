/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "device.h"
#include "daemon.h"
#include "dbusutil.h"

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDebug>

static QString objectPathForId(const QString &id)
{
    QString path = QStringLiteral(TANDEM_DAEMON_PATH "/devices/d_");
    for (const QChar c : id) {
        path += c.isLetterOrNumber() ? c : QLatin1Char('_');
    }
    return path;
}

Device::Device(const QDBusConnection &bus, const QString &id, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_id(id)
    , m_path(objectPathForId(id))
{
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), m_path, QStringLiteral("org.freedesktop.DBus.Properties"),
                  QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), m_path, QStringLiteral(TANDEM_DEVICE_IFACE),
                  QStringLiteral("PluginChanged"), this, SLOT(onPluginChanged(QString)));
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), m_path, QStringLiteral(TANDEM_DEVICE_IFACE),
                  QStringLiteral("SettingsChanged"), this, SLOT(onSettingsChanged()));
    reload();
}

void Device::reload()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    msg << QStringLiteral(TANDEM_DEVICE_IFACE);
    const QDBusMessage reply = m_bus.call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        m_props = Tandem::demarshalMap(reply.arguments().first());
    }
    Q_EMIT changed();
}

void Device::onPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidated)
{
    Q_UNUSED(interface)
    Q_UNUSED(invalidated)
    for (auto it = changedProps.cbegin(); it != changedProps.cend(); ++it) {
        m_props.insert(it.key(), Tandem::demarshal(it.value()));
    }
    Q_EMIT changed();
}

void Device::onPluginChanged(const QString &plugin)
{
    Q_EMIT pluginChanged(plugin);
}

void Device::onSettingsChanged()
{
    m_settingsLoaded = false;
    Q_EMIT settingsChanged();
}

QString Device::name() const
{
    const QString n = prop(QStringLiteral("Name")).toString();
    return n.isEmpty() ? m_id : n;
}

QString Device::type() const
{
    return prop(QStringLiteral("Type")).toString();
}

QString Device::state() const
{
    return prop(QStringLiteral("PairState")).toString();
}

QString Device::key() const
{
    return prop(QStringLiteral("VerificationKey")).toString();
}

bool Device::paired() const
{
    return prop(QStringLiteral("Paired")).toBool();
}

bool Device::reachable() const
{
    return prop(QStringLiteral("Reachable")).toBool();
}

bool Device::paused() const
{
    return prop(QStringLiteral("Paused")).toBool();
}

QStringList Device::plugins() const
{
    return prop(QStringLiteral("Plugins")).toStringList();
}

QString Device::iconName() const
{
    const QString t = type();
    if (t == QLatin1String("phone")) {
        return QStringLiteral("smartphone");
    }
    if (t == QLatin1String("tablet")) {
        return QStringLiteral("tablet");
    }
    if (t == QLatin1String("laptop")) {
        return QStringLiteral("computer-laptop");
    }
    if (t == QLatin1String("tv")) {
        return QStringLiteral("video-television");
    }
    return QStringLiteral("computer");
}

QString Device::statusText() const
{
    const QString s = state();
    if (s == QLatin1String("paired-reachable")) {
        return QStringLiteral("Connected");
    }
    if (s == QLatin1String("paired")) {
        return paused() ? QStringLiteral("Disconnected") : QStringLiteral("Paired, not reachable");
    }
    if (s == QLatin1String("requested")) {
        return QStringLiteral("Pairing requested");
    }
    if (s == QLatin1String("requested-by-peer")) {
        return QStringLiteral("Wants to pair");
    }
    return reachable() ? QStringLiteral("Available") : QStringLiteral("Not reachable");
}

bool Device::hasPlugin(const QString &plugin) const
{
    return plugins().contains(plugin);
}

void Device::call(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral(TANDEM_DEVICE_IFACE), method);
    msg.setArguments(args);
    m_bus.asyncCall(msg, 30000);
}

static QVariantMap typedArgs(const QVariantMap &args)
{
    QVariantMap out;
    for (auto it = args.cbegin(); it != args.cend(); ++it) {
        QVariant v = it.value();
        if (v.typeId() == QMetaType::QVariantList) {
            QStringList strings;
            bool allStrings = true;
            for (const QVariant &item : v.toList()) {
                if (item.typeId() != QMetaType::QString) {
                    allStrings = false;
                    break;
                }
                strings << item.toString();
            }
            if (allStrings) {
                v = strings;
            }
        }
        out.insert(it.key(), v);
    }
    return out;
}

QVariantMap Device::pluginState(const QString &plugin)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral(TANDEM_DEVICE_IFACE), QStringLiteral("GetPluginState"));
    msg << plugin;
    const QDBusMessage reply = m_bus.call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return Tandem::demarshalMap(reply.arguments().first());
}

void Device::callPlugin(const QString &plugin, const QString &method, const QVariantMap &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral(TANDEM_DEVICE_IFACE), QStringLiteral("CallPlugin"));
    msg << plugin << method << typedArgs(args);
    m_bus.asyncCall(msg, 30000);
}

QVariantMap Device::callPluginSync(const QString &plugin, const QString &method, const QVariantMap &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral(TANDEM_DEVICE_IFACE), QStringLiteral("CallPlugin"));
    msg << plugin << method << typedArgs(args);
    const QDBusMessage reply = m_bus.call(msg, QDBus::Block, 30000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qWarning() << "tandem:" << plugin << method << reply.errorMessage();
        return {};
    }
    return Tandem::demarshalMap(reply.arguments().first());
}

QVariantMap Device::settings()
{
    if (!m_settingsLoaded) {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                          QStringLiteral(TANDEM_DEVICE_IFACE), QStringLiteral("GetSettings"));
        const QDBusMessage reply = m_bus.call(msg, QDBus::Block, 5000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_settings = Tandem::demarshalMap(reply.arguments().first());
            m_settingsLoaded = true;
        }
    }
    return m_settings;
}

void Device::setSetting(const QString &key, const QVariant &value)
{
    QVariant typed = value;
    const QVariant current = settings().value(key);
    if (current.isValid() && current.typeId() != typed.typeId()) {
        typed.convert(current.metaType());
    }
    m_settings.insert(key, typed);
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), m_path,
                                                      QStringLiteral(TANDEM_DEVICE_IFACE), QStringLiteral("SetSetting"));
    msg << key << QVariant::fromValue(QDBusVariant(typed));
    m_bus.asyncCall(msg, 5000);
}

QString Device::dataUrl(const QVariant &bytes, const QString &mime) const
{
    const QByteArray data = bytes.toByteArray();
    if (data.isEmpty()) {
        return {};
    }
    return QStringLiteral("data:") + mime + QStringLiteral(";base64,") + QString::fromLatin1(data.toBase64());
}
