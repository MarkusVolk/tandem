import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import de.flk.Tandem

FormCard.FormCardPage {
    id: page

    property var settings: Daemon.settings()
    readonly property var deviceTypes: ["desktop", "laptop"]
    readonly property var backends: ["auto", "portal", "wlr", "uinput"]

    title: "Settings"

    Connections {
        target: Daemon
        function onSettingsChanged() { page.settings = Daemon.settings() }
    }

    FormCard.FormHeader { title: "This device" }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: "Device id"
            description: {
                const id = Daemon.selfId()
                return id.length > 0 ? id : "unknown, tandemd not running"
            }
        }
        FormCard.FormTextFieldDelegate {
            label: "Name shown to other devices"
            text: page.settings["device-name"] !== undefined ? page.settings["device-name"] : ""
            placeholderText: "Host name when empty"
            onEditingFinished: Daemon.setSetting("device-name", text)
        }
        FormCard.FormComboBoxDelegate {
            text: "Device type"
            model: ["Desktop", "Laptop"]
            currentIndex: Math.max(0, page.deviceTypes.indexOf(page.settings["device-type"]))
            onActivated: (index) => Daemon.setSetting("device-type", page.deviceTypes[index])
        }
    }

    FormCard.FormHeader { title: "Behaviour" }
    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            label: "Folder for received files"
            text: page.settings["download-dir"] !== undefined ? page.settings["download-dir"] : ""
            placeholderText: "Downloads folder when empty"
            onEditingFinished: Daemon.setSetting("download-dir", text)
        }
        FormCard.FormSwitchDelegate {
            text: "Discover devices with mDNS"
            description: "Takes effect after a restart"
            checked: page.settings["mdns"] === true
            onToggled: Daemon.setSetting("mdns", checked)
        }
        FormCard.FormSwitchDelegate {
            text: "Show a tray icon"
            description: "Takes effect after a restart"
            checked: page.settings["show-tray"] === true
            onToggled: Daemon.setSetting("show-tray", checked)
        }
        FormCard.FormComboBoxDelegate {
            text: "Remote input backend"
            model: ["Automatic", "RemoteDesktop portal", "wlroots protocols", "uinput"]
            currentIndex: Math.max(0, page.backends.indexOf(page.settings["input-backend"]))
            onActivated: (index) => Daemon.setSetting("input-backend", page.backends[index])
        }
        FormCard.FormSwitchDelegate {
            text: "Share clipboard contents marked as passwords"
            checked: page.settings["send-passwords"] === true
            onToggled: Daemon.setSetting("send-passwords", checked)
        }
    }
}
