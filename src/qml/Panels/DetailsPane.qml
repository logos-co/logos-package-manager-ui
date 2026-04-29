import QtQuick
import QtQuick.Controls

import Logos.Theme

Rectangle {
    id: root

    property string text: ""

    color: Theme.palette.surface
    border.color: Theme.palette.border
    border.width: 1

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.spacing.small

        TextArea {
            text: root.text
            color: Theme.palette.text
            readOnly: true
            wrapMode: Text.Wrap
            background: Rectangle { color: "transparent" }
            font.family: Theme.typography.publicSans
            font.pixelSize: Theme.typography.primaryText
        }
    }
}
