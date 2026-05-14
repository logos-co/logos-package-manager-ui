import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

import icons

// Table header per Figma: title "Packages" + install-state tabs on the
// left, release picker + bulk-action buttons (Reload / Uninstall / Install)
// on the right.
GridLayout {
    id: root

    property bool isInstalling: false
    property bool isLoading: false
    property bool hasInstallableSelection: false
    property bool hasUninstallableSelection: false

    // Install-state tab: 0 = All, 1 = Installed, 2 = Not Installed.
    property int stateIndex: 0
    readonly property alias stateTabs: tabs

    signal reloadClicked()
    signal installClicked()
    signal uninstallClicked()
    signal stateRequested(int state)
    // Multi-repo: opens the "Manage Repositories" popup that the
    // top-level PackageManager.qml hosts. The button lives here for
    // proximity with the other bulk actions; the popup lives at the
    // top level so it can overlay the whole package-manager area.
    signal repositoriesClicked()

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

        // The global "Release" picker that used to live here is removed —
        // each row now carries its own Version dropdown.

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

        // Multi-repo: open the Manage Repositories popup.
        LogosButton {
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            Layout.preferredWidth: 130
            Layout.maximumWidth: 130
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Repositories")
            enabled: !root.isInstalling
            onClicked: root.repositoriesClicked()
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
