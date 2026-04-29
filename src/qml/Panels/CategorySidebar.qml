import QtQuick

import Logos.Theme
import Logos.Controls

Rectangle {
    id: root

    property list<string> categories: []
    property int currentIndex: 0

    signal categorySelected(int index)

    color: Theme.palette.backgroundSecondary

    ListView {
        anchors.fill: parent
        model: root.categories
        currentIndex: root.currentIndex

        delegate: Rectangle {
            width: ListView.view.width
            height: 40
            color: ListView.isCurrentItem
                   ? Theme.palette.surface
                   : (mouse.containsMouse ? Theme.palette.backgroundButton : "transparent")
            radius: Theme.spacing.radiusSmall

            LogosText {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.large
                text: modelData
                color: Theme.palette.text
                font.pixelSize: Theme.typography.primaryText
                verticalAlignment: Text.AlignVCenter
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.categorySelected(index)
            }
        }
    }
}
