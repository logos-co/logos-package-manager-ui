import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

// Page header: Title + Subtitle + Search bar.
RowLayout {
    id: root

    property string searchText: ""

    signal searchEdited(string text)

    spacing: Theme.spacing.large

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing.tiny

        LogosText {
            text: qsTr("Package Manager")
            font.pixelSize: Theme.typography.pageTitleText
            font.weight: Theme.typography.weightRegular
            color: Theme.palette.text
        }

        LogosText {
            text: qsTr("Manage your plugins and packages.")
            font.pixelSize: Theme.typography.subtitleText
            color: Theme.colors.getColor(Theme.palette.text, 0.6)
        }
    }

    Item { Layout.fillWidth: true }

    LogosSearchBar {
        id: searchBar
        Layout.preferredWidth: 605
        Layout.minimumWidth: 200
        text: root.searchText
        placeholderText: qsTr("Search packages…")
        shortcutHint: "⌘K"
        onTextChanged: {
            if (text !== root.searchText)
                root.searchEdited(text)
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        onActivated: {
            searchBar.textInput.forceActiveFocus()
            searchBar.textInput.selectAll()
        }
    }
}
