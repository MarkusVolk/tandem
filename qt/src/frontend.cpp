/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend.h"
#include "dbusutil.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

static const char *const kFrontendName = "de.flk.Tandem";
static const char *const kFrontendPath = "/de/flk/Tandem";

Frontend::Frontend(QObject *parent)
    : QObject(parent)
{
    new FrontendAdaptor(this);
}

bool Frontend::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QLatin1String(kFrontendPath), this)) {
        qWarning() << "tandem: cannot register" << kFrontendPath;
        return false;
    }
    return bus.registerService(QLatin1String(kFrontendName));
}

bool Frontend::activateRunningInstance()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kFrontendName), QLatin1String(kFrontendPath),
                                                      QStringLiteral("org.freedesktop.Application"), QStringLiteral("Activate"));
    msg << QVariantMap();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void Frontend::activate()
{
    Q_EMIT activateRequested();
}

void Frontend::activateAction(const QString &name, const QVariantList &parameters)
{
    QVariant param = parameters.isEmpty() ? QVariant() : Tandem::demarshal(parameters.first());
    if (name == QLatin1String("share")) {
        Q_EMIT shareRequested(param.toString());
    } else if (name == QLatin1String("presenter")) {
        Q_EMIT presenterRequested(param.toString());
    } else if (name == QLatin1String("show-device")) {
        Q_EMIT showDeviceRequested(param.toString());
    } else if (name == QLatin1String("reply")) {
        const QVariantList fields = param.toList();
        if (fields.size() >= 3) {
            Q_EMIT replyRequested(fields.at(0).toString(), fields.at(1).toString(), fields.at(2).toString());
        }
    } else {
        Q_EMIT activateRequested();
    }
}

FrontendAdaptor::FrontendAdaptor(Frontend *parent)
    : QDBusAbstractAdaptor(parent)
    , m_frontend(parent)
{
}

void FrontendAdaptor::Activate(const QVariantMap &platformData)
{
    Q_UNUSED(platformData)
    m_frontend->activate();
}

void FrontendAdaptor::Open(const QStringList &uris, const QVariantMap &platformData)
{
    Q_UNUSED(uris)
    Q_UNUSED(platformData)
    m_frontend->activate();
}

void FrontendAdaptor::ActivateAction(const QString &actionName, const QVariantList &parameter, const QVariantMap &platformData)
{
    Q_UNUSED(platformData)
    m_frontend->activateAction(actionName, parameter);
}
