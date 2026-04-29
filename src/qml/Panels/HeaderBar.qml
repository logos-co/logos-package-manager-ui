import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

import Controls

// Pure composition — explicit state inputs, explicit output signals.
ColumnLayout {
    id: root

    property bool isInstalling: false
    property bool isLoading: false
    property bool hasSelectedPackages: false
    property list<string> releases: []
    property int selectedReleaseIndex: 0

    signal reloadClicked()
    signal installClicked()
    signal releaseSelected(int index)

    spacing: Theme.spacing.xlarge

    LogosText {
        text: qsTr("Package Manager")
        font.pixelSize: Theme.typography.titleText
        font.weight: Theme.typography.weightBold
        color: Theme.palette.text
    }

    LogosText {
        text: qsTr("Manage plugins and packages")
        font.pixelSize: Theme.typography.primaryText
        color: Theme.palette.textSecondary
    }

    RowLayout {
        spacing: Theme.spacing.small

        LogosButton {
            text: qsTr("Reload")
            enabled: !root.isInstalling
            onClicked: root.reloadClicked()
            implicitWidth: 100
            implicitHeight: 32
        }

        InstallButton {
            isInstalling: root.isInstalling
            hasSelectedPackages: root.hasSelectedPackages
            onClicked: root.installClicked()
        }

        Item { Layout.fillWidth: true }

        LogosText {
            text: qsTr("Release:")
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
            verticalAlignment: Text.AlignVCenter
        }

        ReleasePicker {
            releases: root.releases
            currentReleaseIndex: root.selectedReleaseIndex
            isLoading: root.isLoading
            enabled: !(root.isInstalling || root.isLoading)
            onActivated: function(index) { root.releaseSelected(index) }
            Layout.minimumWidth: 200
            Layout.preferredWidth: 200
        }
    }
}
