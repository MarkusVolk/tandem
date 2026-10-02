/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "daemon.h"
#include "device.h"
#include "frontend.h"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tandem-qt"));
    app.setApplicationDisplayName(QStringLiteral("Tandem"));
    app.setOrganizationDomain(QStringLiteral("flk.de"));
    app.setDesktopFileName(QStringLiteral("de.flk.Tandem"));
    app.setApplicationVersion(QStringLiteral(TANDEM_VERSION));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("de.flk.Tandem")));
    app.setQuitOnLastWindowClosed(false);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    const bool service = app.arguments().contains(QStringLiteral("--service"));
    Frontend frontend;
    if (!frontend.registerOnBus()) {
        if (Frontend::activateRunningInstance()) {
            return 0;
        }
    }

    Daemon daemon;
    qmlRegisterUncreatableType<Device>("de.flk.Tandem", 1, 0, "Device", QStringLiteral("Devices come from the daemon"));
    qmlRegisterUncreatableType<DeviceModel>("de.flk.Tandem", 1, 0, "DeviceModel", QStringLiteral("Comes from the daemon"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Daemon"), &daemon);
    engine.rootContext()->setContextProperty(QStringLiteral("Frontend"), &frontend);
    engine.rootContext()->setContextProperty(QStringLiteral("StartHidden"), service);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/de/flk/Tandem/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
