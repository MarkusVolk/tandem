import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.PromptDialog {
    id: dialog

    property var device: null
    property string notificationId: ""

    title: device ? "Reply to " + device.name : "Reply"
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: "Send"
            icon.name: "mail-send"
            enabled: field.text.length > 0
            onTriggered: {
                dialog.device.callPlugin("notifications", "reply", { "id": dialog.notificationId, "text": field.text })
                field.text = ""
                dialog.close()
            }
        }
    ]

    QQC2.TextField {
        id: field
        Layout.fillWidth: true
        placeholderText: "Message"
        onAccepted: if (text.length > 0) {
            dialog.device.callPlugin("notifications", "reply", { "id": dialog.notificationId, "text": text })
            text = ""
            dialog.close()
        }
    }

    onOpened: field.forceActiveFocus()
}
