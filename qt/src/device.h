/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Device : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString type READ type NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString key READ key NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(bool reachable READ reachable NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(QStringList plugins READ plugins NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    Device(const QDBusConnection &bus, const QString &id, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const;
    QString type() const;
    QString state() const;
    QString key() const;
    bool paired() const;
    bool reachable() const;
    bool paused() const;
    QStringList plugins() const;
    QString iconName() const;
    QString statusText() const;

    Q_INVOKABLE bool hasPlugin(const QString &plugin) const;
    Q_INVOKABLE void call(const QString &method, const QVariantList &args = {});
    Q_INVOKABLE QVariantMap pluginState(const QString &plugin);
    Q_INVOKABLE void callPlugin(const QString &plugin, const QString &method, const QVariantMap &args = {});
    Q_INVOKABLE QVariantMap callPluginSync(const QString &plugin, const QString &method, const QVariantMap &args = {});
    Q_INVOKABLE QVariantMap settings();
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE QString dataUrl(const QVariant &bytes, const QString &mime = QStringLiteral("image/jpeg")) const;

Q_SIGNALS:
    void changed();
    void pluginChanged(const QString &plugin);
    void settingsChanged();

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onPluginChanged(const QString &plugin);
    void onSettingsChanged();

private:
    void reload();
    QVariant prop(const QString &name) const { return m_props.value(name); }

    QDBusConnection m_bus;
    QString m_id;
    QString m_path;
    QVariantMap m_props;
    QVariantMap m_settings;
    bool m_settingsLoaded = false;
};
