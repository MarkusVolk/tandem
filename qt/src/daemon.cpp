/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "daemon.h"
#include "dbusutil.h"
#include "device.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDebug>

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size()) {
        return {};
    }
    Device *device = m_devices.at(index.row());
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue(device);
    case IdRole:
        return device->id();
    case NameRole:
    case Qt::DisplayRole:
        return device->name();
    case StateRole:
        return device->state();
    case IconRole:
        return device->iconName();
    case StatusRole:
        return device->statusText();
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        {DeviceRole, "device"}, {IdRole, "deviceId"}, {NameRole, "name"},
        {StateRole, "state"}, {IconRole, "iconName"}, {StatusRole, "statusText"},
    };
}

void DeviceModel::add(Device *device)
{
    if (find(device->id())) {
        device->deleteLater();
        return;
    }
    beginInsertRows(QModelIndex(), m_devices.size(), m_devices.size());
    m_devices.append(device);
    endInsertRows();
    connect(device, &Device::changed, this, [this, device]() {
        const int row = m_devices.indexOf(device);
        if (row >= 0) {
            Q_EMIT dataChanged(index(row), index(row));
        }
    });
    Q_EMIT countChanged();
}

void DeviceModel::remove(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_devices.takeAt(row)->deleteLater();
    endRemoveRows();
    Q_EMIT countChanged();
}

void DeviceModel::clear()
{
    if (m_devices.isEmpty()) {
        return;
    }
    beginResetModel();
    qDeleteAll(m_devices);
    m_devices.clear();
    endResetModel();
    Q_EMIT countChanged();
}

Device *DeviceModel::find(const QString &id) const
{
    for (Device *device : m_devices) {
        if (device->id() == id) {
            return device;
        }
    }
    return nullptr;
}

Device *DeviceModel::at(int row) const
{
    return row >= 0 && row < m_devices.size() ? m_devices.at(row) : nullptr;
}

int DeviceModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_devices.size(); i++) {
        if (m_devices.at(i)->id() == id) {
            return i;
        }
    }
    return -1;
}

Daemon::Daemon(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
{
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), QStringLiteral(TANDEM_DAEMON_PATH), QStringLiteral(TANDEM_MANAGER_IFACE),
                  QStringLiteral("DeviceAdded"), this, SLOT(onDeviceAdded(QString)));
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), QStringLiteral(TANDEM_DAEMON_PATH), QStringLiteral(TANDEM_MANAGER_IFACE),
                  QStringLiteral("DeviceRemoved"), this, SLOT(onDeviceRemoved(QString)));
    m_bus.connect(QStringLiteral(TANDEM_DAEMON_NAME), QStringLiteral(TANDEM_DAEMON_PATH), QStringLiteral(TANDEM_MANAGER_IFACE),
                  QStringLiteral("SettingsChanged"), this, SLOT(onSettingsChanged()));

    auto *watcher = new QDBusServiceWatcher(QStringLiteral(TANDEM_DAEMON_NAME), m_bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &Daemon::onOwnerChanged);

    m_bus.interface()->startService(QStringLiteral(TANDEM_DAEMON_NAME));
    reload();
}

QDBusMessage Daemon::callManager(const QString &method, const QVariantList &args, int timeout)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), QStringLiteral(TANDEM_DAEMON_PATH),
                                                      QStringLiteral(TANDEM_MANAGER_IFACE), method);
    msg.setArguments(args);
    return m_bus.call(msg, QDBus::Block, timeout);
}

void Daemon::reload()
{
    const QDBusMessage reply = callManager(QStringLiteral("ListDevices"), {}, 30000);
    const bool running = reply.type() == QDBusMessage::ReplyMessage;
    if (running != m_running) {
        m_running = running;
        Q_EMIT runningChanged();
    }
    if (!running) {
        qWarning() << "tandem: daemon not reachable:" << reply.errorMessage();
        m_devices.clear();
        return;
    }
    const QVariantMap devices = Tandem::demarshalMap(reply.arguments().value(0));
    for (auto it = devices.cbegin(); it != devices.cend(); ++it) {
        onDeviceAdded(it.key());
    }
}

void Daemon::onDeviceAdded(const QString &id)
{
    if (!m_devices.find(id)) {
        m_devices.add(new Device(m_bus, id, this));
    }
}

void Daemon::onDeviceRemoved(const QString &id)
{
    m_devices.remove(id);
}

void Daemon::onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(name)
    Q_UNUSED(oldOwner)
    if (newOwner.isEmpty()) {
        m_running = false;
        m_devices.clear();
        Q_EMIT runningChanged();
    } else {
        reload();
    }
}

void Daemon::onSettingsChanged()
{
    m_settingsLoaded = false;
    Q_EMIT settingsChanged();
}

void Daemon::refresh()
{
    callManager(QStringLiteral("Refresh"));
}

void Daemon::quitDaemon()
{
    callManager(QStringLiteral("Quit"));
}

QString Daemon::selfId()
{
    const QDBusMessage reply = callManager(QStringLiteral("SelfId"));
    return reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().value(0).toString() : QString();
}

QStringList Daemon::allPlugins()
{
    const QDBusMessage reply = callManager(QStringLiteral("ListPlugins"));
    return reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().value(0).toStringList() : QStringList();
}

QVariantMap Daemon::settings()
{
    if (!m_settingsLoaded) {
        const QDBusMessage reply = callManager(QStringLiteral("GetSettings"));
        if (reply.type() == QDBusMessage::ReplyMessage) {
            m_settings = Tandem::demarshalMap(reply.arguments().value(0));
            m_settingsLoaded = true;
        }
    }
    return m_settings;
}

void Daemon::setSetting(const QString &key, const QVariant &value)
{
    QVariant typed = value;
    const QVariant current = settings().value(key);
    if (current.isValid() && current.typeId() != typed.typeId()) {
        typed.convert(current.metaType());
    }
    m_settings.insert(key, typed);
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral(TANDEM_DAEMON_NAME), QStringLiteral(TANDEM_DAEMON_PATH),
                                                      QStringLiteral(TANDEM_MANAGER_IFACE), QStringLiteral("SetSetting"));
    msg << key << QVariant::fromValue(QDBusVariant(typed));
    m_bus.asyncCall(msg, 5000);
}
