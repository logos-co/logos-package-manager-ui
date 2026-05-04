import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

// Sidebar with two filter sections: Categories on top, Types below,
// separated by a thin divider per Figma. Each section is independent —
// users can pick one category AND one type concurrently.
ColumnLayout {
    id: root

    property list<string> categories: []
    property int currentIndex: 0

    property list<string> types: []
    property int currentTypeIndex: -1

    signal categorySelected(int index)
    signal typeSelected(int index)

    spacing: Theme.spacing.tiny

    LogosText {
        Layout.topMargin: Theme.spacing.tiny
        Layout.bottomMargin: Theme.spacing.tiny
        text: qsTr("Categories")
        font.pixelSize: Theme.typography.subtitleText
        font.weight: Theme.typography.weightRegular
        color: Theme.palette.text
    }

    ListView {
        id: categoriesView
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        interactive: false
        spacing: Theme.spacing.tiny
        model: root.categories
        currentIndex: root.currentIndex

        delegate: SidebarNavItem {
            width: ListView.view.width
            text: modelData
            highlighted: ListView.isCurrentItem
            onClicked: root.categorySelected(index)
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing.xlarge
        Layout.bottomMargin: Theme.spacing.xlarge
        height: 1
        color: Theme.palette.borderSubtle
    }

    LogosText {
        Layout.topMargin: Theme.spacing.tiny
        Layout.bottomMargin: Theme.spacing.tiny
        text: qsTr("Types")
        font.pixelSize: Theme.typography.subtitleText
        font.weight: Theme.typography.weightRegular
        color: Theme.palette.text
    }

    ListView {
        id: typesView
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        interactive: false
        spacing: Theme.spacing.tiny
        model: root.types
        currentIndex: root.currentTypeIndex

        delegate: SidebarNavItem {
            width: ListView.view.width
            text: modelData
            highlighted: ListView.isCurrentItem
            onClicked: root.typeSelected(index)
        }
    }

    Item { Layout.fillWidth: true; Layout.fillHeight: true }

    component SidebarNavItem: LogosItemDelegate {
        id: cell
        radius: Theme.spacing.radiusLarge
        highlightColor: Theme.palette.backgroundButton
        hoverColor: "transparent"
        textColor: (cell.highlighted || cell.hovered)
                       ? Theme.palette.text
                       : Theme.palette.textTertiary
    }
}
