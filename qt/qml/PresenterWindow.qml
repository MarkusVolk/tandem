import QtQuick
import de.flk.Tandem

Window {
    id: win

    required property Device device
    property real dotX: 0.5
    property real dotY: 0.5

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visibility: Window.FullScreen
    visible: true

    function refresh() {
        const state = device.pluginState("presenter")
        if (state.active !== true) {
            win.destroy()
            return
        }
        dotX = state.x
        dotY = state.y
    }

    Connections {
        target: win.device
        function onPluginChanged(name) { if (name === "presenter") win.refresh() }
    }

    Rectangle {
        width: 24
        height: 24
        radius: 12
        color: Qt.rgba(0.9, 0.1, 0.1, 0.85)
        x: win.dotX * win.width - 12
        y: win.dotY * win.height - 12
    }

    Component.onCompleted: refresh()
}
