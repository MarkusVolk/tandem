import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.flk.Tandem

Kirigami.ScrollablePage {
    id: page

    signal deviceSelected(var device)

    title: "Devices"
    Kirigami.ColumnView.fillWidth: false
    implicitWidth: Kirigami.Units.gridUnit * 16

    actions: [
        Kirigami.Action {
            text: "Refresh"
            icon.name: "view-refresh"
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: Daemon.refresh()
        }
    ]

    ListView {
        id: list
        model: Daemon.devices
        currentIndex: -1

        delegate: QQC2.ItemDelegate {
            required property int index
            required property var device
            required property string name
            required property string iconName
            required property string statusText

            width: ListView.view.width
            highlighted: ListView.isCurrentItem
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: iconName
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    QQC2.Label { textFormat: Text.PlainText; text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label { textFormat: Text.PlainText; text: statusText; opacity: 0.7; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
            onClicked: {
                list.currentIndex = index
                page.deviceSelected(device)
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 2
            visible: list.count === 0
            icon.name: "smartphone"
            text: Daemon.running ? "No devices" : "Service not running"
            explanation: Daemon.running ? "Devices running KDE Connect on this network appear here"
                                        : "Start it with systemctl --user start tandem"
        }
    }
}
