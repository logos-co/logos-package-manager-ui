import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls

// Primary (success-colored) Install button with an inline spinner.
Button {
    id: root

    property bool isInstalling: false
    property bool hasSelectedPackages: false

    text: isInstalling ? qsTr("Installing...") : qsTr("Install")
    enabled: hasSelectedPackages && !isInstalling

    contentItem: Row {
        spacing: Theme.spacing.tiny + 2
        anchors.centerIn: parent

        LoadingSpinner {
            width: 14
            height: 14
            thickness: 2
            dotSize: 4
            running: root.isInstalling
        }

        LogosText {
            text: root.text
            font.pixelSize: Theme.typography.secondaryText
            color: root.enabled ? Theme.palette.text : Theme.palette.textMuted
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        implicitWidth: root.isInstalling ? 130 : 100
        implicitHeight: 32
        color: root.enabled
               ? (root.pressed ? Theme.palette.successPressed : Theme.palette.success)
               : Theme.palette.backgroundSecondary
        radius: Theme.spacing.radiusSmall
        border.color: root.enabled ? Theme.palette.successHover : Theme.palette.borderSubtle
        border.width: 1

        Behavior on implicitWidth {
            NumberAnimation { duration: 150 }
        }
    }
}
