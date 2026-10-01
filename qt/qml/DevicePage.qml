import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import de.flk.Tandem

Kirigami.ScrollablePage {
    id: page

    required property Device device

    title: device.name
    padding: 0

    property var battery: ({})
    property var connectivity: ({})
    property var lockState: ({})
    property var notifications: ({})
    property var contacts: ({})
    property var mousepad: ({})
    property var remoteinput: ({})
    property var runcommand: ({})
    property var clipboardState: ({})
    property var deviceSettings: ({})
    property bool notificationsExpanded: false

    function refreshPlugin(name) {
        if (!device.hasPlugin(name))
            return
        const state = device.pluginState(name)
        switch (name) {
        case "battery": battery = state; break
        case "connectivity": connectivity = state; break
        case "lock": lockState = state; break
        case "notifications": notifications = state; break
        case "contacts": contacts = state; break
        case "mousepad": mousepad = state; break
        case "remoteinput": remoteinput = state; break
        case "runcommand": runcommand = state; break
        case "clipboard": clipboardState = state; break
        }
    }

    function refreshAll() {
        for (const name of ["battery", "connectivity", "lock", "notifications", "contacts", "mousepad", "remoteinput", "runcommand", "clipboard"])
            refreshPlugin(name)
        deviceSettings = device.settings()
    }

    Component.onCompleted: refreshAll()

    Connections {
        target: page.device
        function onPluginChanged(name) { page.refreshPlugin(name) }
        function onChanged() { page.refreshAll() }
        function onSettingsChanged() { page.deviceSettings = page.device.settings() }
    }

    actions: [
        Kirigami.Action {
            text: "Pair"
            icon.name: "network-connect"
            visible: !page.device.paired && page.device.state !== "requested-by-peer" && page.device.reachable
            onTriggered: page.device.call("Pair")
        },
        Kirigami.Action {
            text: "Accept"
            icon.name: "dialog-ok"
            visible: page.device.state === "requested-by-peer"
            onTriggered: page.device.call("AcceptPair")
        },
        Kirigami.Action {
            text: "Reject"
            icon.name: "dialog-cancel"
            visible: page.device.state === "requested-by-peer" || page.device.state === "requested"
            onTriggered: page.device.call("RejectPair")
        },
        Kirigami.Action {
            text: page.device.paused ? "Connect" : "Disconnect"
            icon.name: page.device.paused ? "network-connect" : "network-disconnect"
            visible: page.device.paired
            onTriggered: page.device.call(page.device.paused ? "Connect" : "Disconnect")
        },
        Kirigami.Action {
            text: "Unpair"
            icon.name: "edit-delete"
            visible: page.device.paired
            onTriggered: page.device.call("Unpair")
        }
    ]

    header: QQC2.TabBar {
        id: tabs
        visible: page.device.paired
        QQC2.TabButton { text: "Device" }
        QQC2.TabButton { text: "Messages"; visible: page.device.hasPlugin("sms") || page.device.hasPlugin("notifications") || page.device.hasPlugin("contacts") || page.device.hasPlugin("telephony"); width: visible ? implicitWidth : 0 }
        QQC2.TabButton { text: "Sharing"; visible: page.device.hasPlugin("share") || page.device.hasPlugin("clipboard") || page.device.hasPlugin("sendnotifications"); width: visible ? implicitWidth : 0 }
        QQC2.TabButton { text: "Control"; visible: page.device.hasPlugin("remoteinput") || page.device.hasPlugin("runcommand") || page.device.hasPlugin("mousepad"); width: visible ? implicitWidth : 0 }
        QQC2.TabButton { text: "Plugins" }
    }

    ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: page.device.state === "requested" || page.device.state === "requested-by-peer"
            type: Kirigami.MessageType.Information
            text: (page.device.state === "requested-by-peer" ? "This device asks to pair." : "Waiting for the device to accept.")
                  + (page.device.key.length > 0 ? " Verification key: " + page.device.key : "")
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: page.device.paired ? tabs.currentIndex : 0

            ColumnLayout {
                spacing: 0
                FormCard.FormHeader { title: "Status" }
                FormCard.FormCard {
                    FormCard.FormTextDelegate { text: "Status"; description: page.device.statusText }
                    FormCard.FormButtonDelegate {
                        visible: page.device.paired && page.device.hasPlugin("findmyphone")
                        text: "Ring the device"
                        icon.name: "preferences-desktop-notification-bell"
                        enabled: page.device.reachable
                        onClicked: page.device.callPlugin("findmyphone", "ring")
                    }
                    FormCard.FormButtonDelegate {
                        visible: page.device.paired
                        text: "Send ping"
                        icon.name: "network-transmit"
                        enabled: page.device.reachable
                        onClicked: page.device.call("Ping", [""])
                    }
                }
                FormCard.FormHeader { title: "Battery"; visible: page.device.hasPlugin("battery") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("battery")
                    FormCard.FormTextDelegate {
                        text: "Charge"
                        description: page.battery.charge === undefined || page.battery.charge < 0 ? "Unknown"
                                     : page.battery.charge + " %" + (page.battery.charging ? ", charging" : "")
                        leading: Kirigami.Icon {
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                            source: page.battery.charging ? "battery-charging" : (page.battery.charge < 15 ? "battery-caution" : "battery-full")
                        }
                    }
                }
                FormCard.FormHeader { title: "Mobile network"; visible: page.device.hasPlugin("connectivity") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("connectivity")
                    FormCard.FormTextDelegate { text: "Signal"; description: page.connectivity.summary ? page.connectivity.summary : "No signal" }
                }
                FormCard.FormHeader { title: "Lock"; visible: page.device.hasPlugin("lock") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("lock")
                    FormCard.FormSwitchDelegate {
                        text: "Device locked"
                        checked: page.lockState.locked === true
                        onToggled: page.device.callPlugin("lock", "lock", { "locked": checked })
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                FormCard.FormHeader { title: "Messages"; visible: page.device.hasPlugin("sms") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("sms")
                    FormCard.FormButtonDelegate {
                        text: "Open conversations"
                        icon.name: "mail-message"
                        onClicked: applicationWindow().pageStack.push(smsPage, { device: page.device })
                    }
                }
                FormCard.FormHeader {
                    visible: page.device.hasPlugin("notifications")
                    title: {
                        const n = page.notifications.notifications ? page.notifications.notifications.length : 0
                        return n === 1 ? "1 notification" : n + " notifications"
                    }
                }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("notifications")
                    FormCard.FormButtonDelegate {
                        text: page.notificationsExpanded ? "Hide" : "Show"
                        icon.name: page.notificationsExpanded ? "go-up" : "go-down"
                        enabled: page.notifications.notifications && page.notifications.notifications.length > 0
                        onClicked: page.notificationsExpanded = !page.notificationsExpanded
                    }
                    Repeater {
                        model: page.notificationsExpanded && page.notifications.notifications ? page.notifications.notifications : []
                        delegate: FormCard.AbstractFormDelegate {
                            required property var modelData
                            background: null
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    QQC2.Label { text: modelData.title && modelData.title.length > 0 ? modelData.title : modelData.text; elide: Text.ElideRight; Layout.fillWidth: true }
                                    QQC2.Label { text: modelData.app; opacity: 0.7; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                QQC2.ToolButton {
                                    visible: modelData.replyable === true
                                    icon.name: "mail-reply-sender"
                                    onClicked: {
                                        replyDialog.device = page.device
                                        replyDialog.notificationId = modelData.id
                                        replyDialog.subtitle = modelData.title
                                        replyDialog.open()
                                    }
                                }
                                QQC2.ToolButton {
                                    icon.name: "window-close"
                                    onClicked: page.device.callPlugin("notifications", "dismiss", { "id": modelData.id })
                                }
                            }
                        }
                    }
                }
                FormCard.FormHeader { title: "Contacts"; visible: page.device.hasPlugin("contacts") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("contacts")
                    FormCard.FormButtonDelegate {
                        text: "Synchronise now"
                        description: (page.contacts.count !== undefined ? page.contacts.count : 0) + " contacts" + (page.contacts.syncing ? ", syncing" : "")
                        icon.name: "view-refresh"
                        onClicked: page.device.callPlugin("contacts", "sync")
                    }
                }
                FormCard.FormHeader { title: "Telephony"; visible: page.device.hasPlugin("telephony") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("telephony")
                    FormCard.FormSwitchDelegate {
                        text: "Pause media during calls"
                        checked: page.deviceSettings["pause-media-on-call"] === true
                        onToggled: page.device.setSetting("pause-media-on-call", checked)
                    }
                    FormCard.FormSwitchDelegate {
                        text: "Mute the speakers during calls"
                        checked: page.deviceSettings["mute-on-call"] === true
                        onToggled: page.device.setSetting("mute-on-call", checked)
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                FormCard.FormHeader { title: "Share"; visible: page.device.hasPlugin("share") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("share")
                    FormCard.FormButtonDelegate {
                        text: "Send files"
                        icon.name: "document-send"
                        onClicked: applicationWindow().shareWith(page.device)
                    }
                    FormCard.FormTextFieldDelegate {
                        id: shareText
                        label: "Send text"
                        placeholderText: "Text or link"
                        onAccepted: {
                            if (text.length === 0)
                                return
                            const isUrl = text.indexOf("://") > 0
                            page.device.callPlugin("share", isUrl ? "url" : "text", isUrl ? { "url": text } : { "text": text })
                            text = ""
                        }
                    }
                }
                FormCard.FormHeader { title: "Clipboard"; visible: page.device.hasPlugin("clipboard") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("clipboard")
                    FormCard.FormSwitchDelegate {
                        text: "Share automatically"
                        description: page.clipboardState.backend ? "Clipboard access through " + page.clipboardState.backend : ""
                        checked: page.deviceSettings["clipboard-auto"] === true
                        onToggled: page.device.setSetting("clipboard-auto", checked)
                    }
                    FormCard.FormButtonDelegate {
                        text: "Send clipboard now"
                        icon.name: "edit-paste"
                        onClicked: page.device.callPlugin("clipboard", "send")
                    }
                }
                FormCard.FormHeader { title: "Desktop notifications"; visible: page.device.hasPlugin("sendnotifications") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("sendnotifications")
                    FormCard.FormSwitchDelegate {
                        text: "Forward notifications to the device"
                        checked: page.deviceSettings["send-notifications"] === true
                        onToggled: page.device.setSetting("send-notifications", checked)
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                FormCard.FormHeader { title: "Type on the device"; visible: page.device.hasPlugin("remoteinput") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("remoteinput")
                    FormCard.FormTextFieldDelegate {
                        label: "Text to send"
                        placeholderText: page.remoteinput.keyboard ? "The device accepts keyboard input" : "Open a text field on the device first"
                        onAccepted: {
                            if (text.length === 0)
                                return
                            page.device.callPlugin("remoteinput", "text", { "text": text })
                            text = ""
                        }
                    }
                    FormCard.AbstractFormDelegate {
                        background: null
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label { text: "Keys"; Layout.fillWidth: true }
                            Repeater {
                                model: [["Backspace", 1], ["Enter", 12], ["Tab", 2], ["Esc", 14]]
                                delegate: QQC2.Button {
                                    required property var modelData
                                    text: modelData[0]
                                    onClicked: page.device.callPlugin("remoteinput", "special", { "key": modelData[1] })
                                }
                            }
                        }
                    }
                }
                FormCard.FormHeader { title: "Commands the device may run here"; visible: page.device.hasPlugin("runcommand") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("runcommand")
                    Repeater {
                        model: page.runcommand.commands ? page.runcommand.commands : []
                        delegate: FormCard.AbstractFormDelegate {
                            required property var modelData
                            background: null
                            contentItem: RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    QQC2.Label { text: modelData[1]; elide: Text.ElideRight; Layout.fillWidth: true }
                                    QQC2.Label { text: modelData[2]; opacity: 0.7; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                QQC2.ToolButton {
                                    icon.name: "edit-delete"
                                    onClicked: page.device.callPlugin("runcommand", "remove", { "key": modelData[0] })
                                }
                            }
                        }
                    }
                    FormCard.FormButtonDelegate {
                        text: "Add command"
                        icon.name: "list-add"
                        onClicked: addCommandDialog.open()
                    }
                }
                FormCard.FormHeader { title: "Remote input"; visible: page.device.hasPlugin("mousepad") }
                FormCard.FormCard {
                    visible: page.device.hasPlugin("mousepad")
                    FormCard.FormTextDelegate {
                        text: "Backend"
                        description: !page.mousepad.backend || page.mousepad.backend === "none" ? "No input backend available"
                                     : page.mousepad.backend + ", keyboard " + (page.mousepad.keyboard ? "yes" : "no") + ", pointer " + (page.mousepad.pointer ? "yes" : "no")
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                FormCard.FormHeader { title: "Plugins" }
                FormCard.FormCard {
                    Repeater {
                        model: Daemon.allPlugins()
                        delegate: FormCard.FormSwitchDelegate {
                            required property string modelData
                            text: PluginTitles.title(modelData)
                            checked: {
                                const disabled = page.deviceSettings["disabled-plugins"]
                                return !(disabled && disabled.indexOf(modelData) >= 0)
                            }
                            onToggled: page.device.call("SetPluginEnabled", [modelData, checked])
                        }
                    }
                }
            }
        }
    }

    Component {
        id: smsPage
        SmsPage {}
    }

    ReplyDialog {
        id: replyDialog
    }

    AddCommandDialog {
        id: addCommandDialog
        device: page.device
    }
}
