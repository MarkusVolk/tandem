/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Frontend : public QObject
{
    Q_OBJECT

public:
    explicit Frontend(QObject *parent = nullptr);

    bool registerOnBus();
    static bool activateRunningInstance();

Q_SIGNALS:
    void activateRequested();
    void showDeviceRequested(const QString &deviceId);
    void shareRequested(const QString &deviceId);
    void replyRequested(const QString &deviceId, const QString &notificationId, const QString &title);
    void presenterRequested(const QString &deviceId);

public:
    void activate();
    void activateAction(const QString &name, const QVariantList &parameters);
};

class FrontendAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit FrontendAdaptor(Frontend *parent);

public Q_SLOTS:
    void Activate(const QVariantMap &platformData);
    void Open(const QStringList &uris, const QVariantMap &platformData);
    void ActivateAction(const QString &actionName, const QVariantList &parameter, const QVariantMap &platformData);

private:
    Frontend *m_frontend;
};
