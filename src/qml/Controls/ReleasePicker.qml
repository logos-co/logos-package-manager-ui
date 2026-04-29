import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls

// Themed ComboBox for selecting a GitHub release tag.
ComboBox {
    id: root

    property list<string> releases: []
    property int currentReleaseIndex: 0
    property bool isLoading: false

    model: releases
    currentIndex: currentReleaseIndex
    implicitHeight: 32

    displayText: (isLoading && (releases ? releases.length : 0) <= 1)
                 ? qsTr("Loading releases…")
                 : currentText

    contentItem: LogosText {
        leftPadding: 10
        rightPadding: root.indicator ? root.indicator.width + 10 : 30
        text: root.displayText
        font.pixelSize: Theme.typography.secondaryText
        color: root.enabled ? Theme.palette.text : Theme.palette.textMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
    }

    background: Rectangle {
        color: root.enabled
               ? (root.pressed ? Theme.palette.pressed : Theme.palette.backgroundButton)
               : Theme.palette.backgroundSecondary
        radius: Theme.spacing.radiusSmall
        border.color: root.enabled ? Theme.palette.border : Theme.palette.borderSubtle
        border.width: 1
    }

    popup: Popup {
        y: root.height + 2
        width: root.width
        implicitHeight: contentItem.implicitHeight
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            color: Theme.palette.backgroundSecondary
            border.color: Theme.palette.border
            radius: Theme.spacing.radiusSmall
        }
    }

    delegate: ItemDelegate {
        width: root.width
        contentItem: LogosText {
            text: modelData
            color: Theme.palette.text
            font.pixelSize: Theme.typography.secondaryText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        highlighted: root.highlightedIndex === index
        background: Rectangle {
            color: highlighted ? Theme.palette.surface : "transparent"
        }
    }
}
