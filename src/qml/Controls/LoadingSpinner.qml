import QtQuick

import Logos.Theme

// Rotating ring + dot. Used for the install-button spinner and the
// package-list loading overlay.
Rectangle {
    id: root

    property bool running: false
    property int thickness: 2
    property int dotSize: 4
    property int dotMargin: 1
    property color ringColor: Theme.palette.text

    color: "transparent"
    border.color: ringColor
    border.width: thickness
    radius: width / 2
    visible: running

    Rectangle {
        width: root.dotSize
        height: root.dotSize
        radius: width / 2
        color: root.ringColor
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.dotMargin
    }

    RotationAnimation on rotation {
        from: 0
        to: 360
        duration: 1000
        loops: Animation.Infinite
        running: root.running
    }
}
