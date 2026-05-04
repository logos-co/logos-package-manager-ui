import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

import Icons

// Table header per Figma: title "Packages" + install-state tabs on the
// left, release picker + bulk-action buttons (Reload / Uninstall / Install)
// on the right.
GridLayout {
    id: root

    property bool isInstalling: false
    property bool isLoading: false
    property bool hasInstallableSelection: false
    property bool hasUninstallableSelection: false
    property list<string> releases: []
    property int selectedReleaseIndex: 0

    // Install-state tab: 0 = All, 1 = Installed, 2 = Not Installed.
    property int stateIndex: 0
    readonly property alias stateTabs: tabs

    signal reloadClicked()
    signal installClicked()
    signal uninstallClicked()
    signal releaseSelected(int index)
    signal stateRequested(int state)

    columnSpacing: Theme.spacing.large
    rowSpacing: Theme.spacing.medium
    columns: (leftHalf.implicitWidth + rightHalf.implicitWidth + columnSpacing) <= width
             ? 2 : 1

    // ─── Left half: title + install-state tabs ───
    RowLayout {
        id: leftHalf
        Layout.fillWidth: true
        spacing: Theme.spacing.large

        LogosText {
            text: qsTr("Packages")
            font.pixelSize: Theme.typography.panelTitleText
            font.weight: Theme.typography.weightMedium
            color: Theme.palette.text
        }

        LogosTabBar {
            id: tabs
            spacing: Theme.spacing.large

            currentIndex: root.stateIndex
            onCurrentIndexChanged: {
                if (currentIndex !== root.stateIndex)
                    root.stateRequested(currentIndex)
            }

            LogosTabButton { text: qsTr("All"); iconSource: PackageIcons.pages }
            LogosTabButton { text: qsTr("Installed") }
            LogosTabButton { text: qsTr("Not Installed") }
        }

        Item { Layout.fillWidth: true }
    }

    // ─── Right half: release picker + bulk actions ───
    RowLayout {
        id: rightHalf
        Layout.fillWidth: true
        spacing: Theme.spacing.medium

        Item { Layout.fillWidth: root.columns === 2 }

        LogosComboBox {
            Layout.fillWidth: true
            Layout.minimumWidth: 130
            Layout.preferredWidth: 200
            Layout.maximumWidth: 200

            model: root.releases
            currentIndex: root.selectedReleaseIndex
            enabled: !(root.isInstalling || root.isLoading)

            displayText: (root.isLoading && root.releases.length <= 1)
                         ? qsTr("Loading releases…")
                         : currentText

            onActivated: function(index) { root.releaseSelected(index) }
        }

        LogosButton {
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.preferredWidth: 100
            Layout.maximumWidth: 100
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Reload")
            enabled: !root.isInstalling && !root.isLoading
            onClicked: root.reloadClicked()
        }

        LogosButton {
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.preferredWidth: 100
            Layout.maximumWidth: 100
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Uninstall")
            enabled: root.hasUninstallableSelection && !root.isInstalling
            onClicked: root.uninstallClicked()
        }

        LogosButton {
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.preferredWidth: 100
            Layout.maximumWidth: 100
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Install")
            enabled: root.hasInstallableSelection && !root.isInstalling
            onClicked: root.installClicked()
        }
    }
}
