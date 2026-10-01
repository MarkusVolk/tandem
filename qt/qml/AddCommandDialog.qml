import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.PromptDialog {
    id: dialog

    property var device: null

    title: "Add command"
    subtitle: "The device will be able to run this command here."
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: "Add"
            icon.name: "list-add"
            enabled: nameField.text.length > 0 && commandField.text.length > 0
            onTriggered: {
                dialog.device.callPlugin("runcommand", "add", { "name": nameField.text, "command": commandField.text })
                nameField.text = ""
                commandField.text = ""
                dialog.close()
            }
        }
    ]

    ColumnLayout {
        QQC2.TextField { id: nameField; Layout.fillWidth: true; placeholderText: "Name" }
        QQC2.TextField { id: commandField; Layout.fillWidth: true; placeholderText: "Command" }
    }
}
