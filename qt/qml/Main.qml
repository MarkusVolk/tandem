import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.ApplicationWindow {
    id: root

    title: "Tandem"
    width: Kirigami.Units.gridUnit * 50
    height: Kirigami.Units.gridUnit * 34
    minimumWidth: Kirigami.Units.gridUnit * 20
    visible: !StartHidden

    pageStack.initialPage: DevicesPage {
        id: devicesPage
        onDeviceSelected: (device) => root.showDevice(device)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: true
        actions: [
            Kirigami.Action {
                text: "Refresh"
                icon.name: "view-refresh"
                onTriggered: Daemon.refresh()
            },
            Kirigami.Action {
                text: "Settings"
                icon.name: "configure"
                onTriggered: root.pageStack.pushDialogLayer(settingsPage, {}, { title: "Settings" })
            },
            Kirigami.Action {
                text: "About Tandem"
                icon.name: "help-about"
                onTriggered: root.pageStack.pushDialogLayer(aboutPage, {}, { title: "About Tandem" })
            },
            Kirigami.Action {
                text: "Quit"
                icon.name: "application-exit"
                onTriggered: Qt.quit()
            }
        ]
    }

    function showDevice(device) {
        if (!device)
            return
        while (root.pageStack.depth > 1)
            root.pageStack.pop()
        root.pageStack.push(devicePage, { device: device })
    }

    function shareWith(device) {
        shareDialog.device = device
        if (device)
            shareDialog.open()
    }

    function bringUp() {
        root.visible = true
        root.show()
        root.raise()
        root.requestActivate()
    }

    Component {
        id: devicePage
        DevicePage {}
    }

    Component {
        id: settingsPage
        SettingsPage {}
    }

    Component {
        id: aboutPage
        Kirigami.AboutPage {
            aboutData: ({
                displayName: "Tandem",
                productName: "tandem",
                componentName: "tandem",
                shortDescription: "A KDE Connect client",
                homepage: "https://github.com/MarkusVolk/tandem",
                bugAddress: "",
                version: Qt.application.version,
                programLogo: "de.flk.Tandem",
                licenses: [{ name: "GPL-2.0-or-later", text: "", spdx: "GPL-2.0-or-later" }],
                authors: [{ name: "Markus Volk" }],
                copyrightStatement: "",
                desktopFileName: "de.flk.Tandem"
            })
        }
    }

    Component {
        id: presenterWindow
        PresenterWindow {}
    }

    FileDialog {
        id: shareDialog
        property var device: null
        title: device ? "Send files to " + device.name : "Send files"
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (!device)
                return
            let paths = []
            for (const url of selectedFiles) {
                let s = url.toString()
                if (s.startsWith("file://"))
                    s = decodeURIComponent(s.substring(7))
                paths.push(s)
            }
            device.callPluginSync("share", "files", { "paths": paths })
        }
    }

    ReplyDialog {
        id: replyDialog
    }

    Connections {
        target: Frontend
        function onActivateRequested() { root.bringUp() }
        function onShowDeviceRequested(id) {
            root.bringUp()
            root.showDevice(Daemon.find(id))
        }
        function onShareRequested(id) { root.shareWith(Daemon.find(id)) }
        function onReplyRequested(id, notificationId, title) {
            const device = Daemon.find(id)
            if (!device)
                return
            root.bringUp()
            replyDialog.device = device
            replyDialog.notificationId = notificationId
            replyDialog.subtitle = title
            replyDialog.open()
        }
        function onPresenterRequested(id) {
            const device = Daemon.find(id)
            if (device)
                presenterWindow.createObject(root, { device: device })
        }
    }

    Connections {
        target: Daemon.devices
        function onCountChanged() {
            if (Daemon.devices.count > 0 && root.pageStack.depth === 1)
                root.showDevice(Daemon.devices.at(0))
            if (Daemon.devices.count === 0)
                while (root.pageStack.depth > 1)
                    root.pageStack.pop()
        }
    }

    Component.onCompleted: {
        if (Daemon.devices.count > 0)
            root.showDevice(Daemon.devices.at(0))
    }
}
