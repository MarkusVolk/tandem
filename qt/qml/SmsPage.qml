import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.ScrollablePage {
    id: page

    required property Device device
    property var threads: []

    title: "Messages"

    function formatDate(ms) {
        if (!ms)
            return ""
        const d = new Date(ms)
        const now = new Date()
        if (d.toDateString() === now.toDateString())
            return Qt.formatTime(d, "hh:mm")
        return Qt.formatDateTime(d, "dd.MM.yyyy hh:mm")
    }

    function reload() {
        const state = device.pluginState("sms")
        threads = state.threads ? state.threads : []
    }

    actions: [
        Kirigami.Action {
            text: "Refresh"
            icon.name: "view-refresh"
            onTriggered: page.device.callPlugin("sms", "threads")
        }
    ]

    Connections {
        target: page.device
        function onPluginChanged(name) { if (name === "sms") page.reload() }
    }

    Component.onCompleted: {
        reload()
        if (device.reachable)
            device.callPlugin("sms", "threads")
    }

    ListView {
        id: list
        model: page.threads

        delegate: QQC2.ItemDelegate {
            required property var modelData
            width: ListView.view.width
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    visible: modelData.last && modelData.last.read === false && modelData.last.outgoing === false
                    source: "mail-unread"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label { textFormat: Text.PlainText; text: modelData.title; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        text: modelData.last ? modelData.last.body.replace(/\n/g, " ") : ""
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                QQC2.Label { textFormat: Text.PlainText; text: modelData.last ? page.formatDate(modelData.last.date) : ""; opacity: 0.7; font: Kirigami.Theme.smallFont }
            }
            onClicked: applicationWindow().pageStack.push(conversationPage, { device: page.device, thread: modelData })
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 2
            visible: list.count === 0
            icon.name: "mail-message"
            text: "No conversations yet"
            explanation: "Conversations are fetched from the phone when it is connected and allows it."
        }
    }

    Component {
        id: conversationPage
        ConversationPage {}
    }
}
