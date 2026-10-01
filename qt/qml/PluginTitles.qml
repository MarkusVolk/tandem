pragma Singleton
import QtQuick

QtObject {
    readonly property var titles: ({
        "ping": "Ping",
        "battery": "Battery",
        "connectivity": "Mobile network",
        "findmyphone": "Find my phone",
        "lock": "Lock",
        "runcommand": "Run commands",
        "notifications": "Notifications",
        "share": "Share",
        "mpriscontrol": "Media control",
        "systemvolume": "System volume",
        "mprisremote": "Remote media",
        "mousepad": "Remote input",
        "presenter": "Presenter",
        "contacts": "Contacts",
        "telephony": "Telephony",
        "sms": "Messages",
        "clipboard": "Clipboard",
        "sendnotifications": "Send notifications",
        "remoteinput": "Remote keyboard"
    })

    function title(name) {
        return titles[name] !== undefined ? titles[name] : name
    }
}
