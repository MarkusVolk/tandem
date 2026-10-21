import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.ScrollablePage {
    id: page

    required property Device device
    required property var thread
    property var messages: []
    property bool scrollToEnd: true

    title: thread.title

    function formatDate(ms) {
        const d = new Date(ms)
        const now = new Date()
        if (d.toDateString() === now.toDateString())
            return Qt.formatTime(d, "hh:mm")
        return Qt.formatDateTime(d, "dd.MM.yyyy hh:mm")
    }

    function reload() {
        const result = device.callPluginSync("sms", "messages", { "thread": thread.id })
        messages = result.messages ? result.messages : []
        if (scrollToEnd)
            Qt.callLater(() => list.positionViewAtEnd())
    }

    function requestOlder() {
        scrollToEnd = false
        const before = messages.length > 0 ? messages[0].date : 0
        device.callPlugin("sms", "request-messages", { "thread": thread.id, "before": before, "count": 50 })
    }

    function send() {
        if (input.text.length === 0)
            return
        device.callPlugin("sms", "send", { "addresses": thread.addresses, "body": input.text })
        input.text = ""
        scrollToEnd = true
    }

    Connections {
        target: page.device
        function onPluginChanged(name) { if (name === "sms") page.reload() }
    }

    Component.onCompleted: {
        reload()
        if (thread.loaded !== true)
            device.callPlugin("sms", "request-messages", { "thread": thread.id, "before": 0, "count": 50 })
    }

    header: QQC2.ToolBar {
        contentItem: QQC2.ToolButton {
            text: "Load older messages"
            icon.name: "go-up"
            enabled: page.messages.length > 0
            onClicked: page.requestOlder()
        }
    }

    footer: QQC2.ToolBar {
        contentItem: RowLayout {
            QQC2.TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: "Message"
                onAccepted: page.send()
            }
            QQC2.ToolButton {
                icon.name: "mail-send"
                enabled: input.text.length > 0
                onClicked: page.send()
            }
        }
    }

    ListView {
        id: list
        model: page.messages
        spacing: Kirigami.Units.smallSpacing
        verticalLayoutDirection: ListView.TopToBottom

        delegate: Item {
            required property var modelData
            width: ListView.view.width
            implicitHeight: bubble.implicitHeight + Kirigami.Units.smallSpacing

            Kirigami.AbstractCard {
                id: bubble
                anchors.left: modelData.outgoing ? undefined : parent.left
                anchors.right: modelData.outgoing ? parent.right : undefined
                anchors.leftMargin: modelData.outgoing ? 0 : Kirigami.Units.largeSpacing
                anchors.rightMargin: modelData.outgoing ? Kirigami.Units.largeSpacing : 0
                width: Math.min(parent.width * 0.75, Math.max(contentColumn.implicitWidth + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6))
                contentItem: ColumnLayout {
                    id: contentColumn
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        textFormat: Text.PlainText
                        visible: modelData.body.length > 0
                        text: modelData.body
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: modelData.attachments ? modelData.attachments : []
                        delegate: QQC2.ToolButton {
                            required property var attachment
                            required property int index
                            property var att: modelData.attachments[index]
                            Layout.preferredWidth: image.status === Image.Ready ? 160 : implicitWidth
                            Layout.preferredHeight: image.status === Image.Ready ? 120 : implicitHeight
                            text: image.status === Image.Ready ? "" : att.mime_type
                            contentItem: Image {
                                id: image
                                source: att.thumbnail ? page.device.dataUrl(att.thumbnail) : ""
                                fillMode: Image.PreserveAspectFit
                            }
                            onClicked: page.device.callPlugin("sms", "attachment", { "part_id": att.part_id, "unique_identifier": att.unique_identifier })
                        }
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        text: page.formatDate(modelData.date)
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        Layout.alignment: modelData.outgoing ? Qt.AlignRight : Qt.AlignLeft
                    }
                }
            }
        }
    }
}
