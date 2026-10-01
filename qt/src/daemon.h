/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

#define TANDEM_DAEMON_NAME "de.flk.Tandem.Daemon"
#define TANDEM_DAEMON_PATH "/de/flk/Tandem"
#define TANDEM_MANAGER_IFACE "de.flk.Tandem.Manager"
#define TANDEM_DEVICE_IFACE "de.flk.Tandem.Device"

class Device;

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { DeviceRole = Qt::UserRole + 1, IdRole, NameRole, StateRole, IconRole, StatusRole };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void add(Device *device);
    void remove(const QString &id);
    void clear();
    Device *find(const QString &id) const;
    Q_INVOKABLE Device *at(int row) const;
    Q_INVOKABLE int indexOf(const QString &id) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<Device *> m_devices;
};

class Daemon : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DeviceModel *devices READ devices CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit Daemon(QObject *parent = nullptr);

    DeviceModel *devices() { return &m_devices; }
    bool running() const { return m_running; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void quitDaemon();
    Q_INVOKABLE QString selfId();
    Q_INVOKABLE QStringList allPlugins();
    Q_INVOKABLE QVariantMap settings();
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE Device *find(const QString &id) const { return m_devices.find(id); }

Q_SIGNALS:
    void runningChanged();
    void settingsChanged();

private Q_SLOTS:
    void onDeviceAdded(const QString &id);
    void onDeviceRemoved(const QString &id);
    void onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onSettingsChanged();

private:
    void reload();
    QDBusMessage callManager(const QString &method, const QVariantList &args = {}, int timeout = 5000);

    QDBusConnection m_bus;
    DeviceModel m_devices;
    QVariantMap m_settings;
    bool m_settingsLoaded = false;
    bool m_running = false;
};
