import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

import Controls

Rectangle {
    id: root

    property var packagesModel
    property bool isLoading: false

    signal detailsRequested(int index)
    signal selectionToggled(int index, bool checked)
    signal upgradeRequested(int index)
    signal downgradeRequested(int index)
    signal reinstallRequested(int index)
    signal uninstallRequested(int index)

    color: Theme.palette.surface
    border.color: Theme.palette.backgroundBlack
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.small
        spacing: 0
        opacity: root.isLoading ? 0.4 : 1.0
        enabled: !root.isLoading

        Rectangle {
            Layout.fillWidth: true
            height: 35
            color: Theme.palette.surface

            RowLayout {
                anchors.fill: parent
                spacing: 0

                HeaderColumn { headerText: qsTr("S"); centerAlign: true; columnWidth: 40 }
                HeaderColumn { headerText: qsTr("Package"); columnWidth: 150 }
                HeaderColumn { headerText: qsTr("Type"); columnWidth: 80 }
                HeaderColumn { headerText: qsTr("Description"); fillWidth: true }
                HeaderColumn { headerText: qsTr("Status"); columnWidth: 120 }
                HeaderColumn { headerText: qsTr("Actions"); columnWidth: 110; centerAlign: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.palette.border
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.packagesModel

            delegate: PackageRow {
                row: model
                rowIndex: index
                zebra: index % 2 !== 0
                onDetailsRequested: function(idx) { root.detailsRequested(idx) }
                onSelectionToggled: function(idx, checked) { root.selectionToggled(idx, checked) }
                onUpgradeRequested: function(idx) { root.upgradeRequested(idx) }
                onDowngradeRequested: function(idx) { root.downgradeRequested(idx) }
                onReinstallRequested: function(idx) { root.reinstallRequested(idx) }
                onUninstallRequested: function(idx) { root.uninstallRequested(idx) }
            }
        }
    }

    // Loading spinner overlay (covers the package list pane only so the
    // category sidebar stays responsive).
    LoadingSpinner {
        anchors.centerIn: parent
        width: 36
        height: 36
        thickness: 3
        dotSize: 6
        dotMargin: 2
        running: root.isLoading
    }

    // Sticky column header cell
    component HeaderColumn: Rectangle {
        property string headerText: ""
        property bool centerAlign: false
        property int columnWidth: 80
        property bool fillWidth: false

        Layout.preferredWidth: fillWidth ? -1 : columnWidth
        Layout.fillWidth: fillWidth
        Layout.fillHeight: true
        color: "transparent"

        LogosText {
            anchors.left: centerAlign ? undefined : parent.left
            anchors.leftMargin: centerAlign ? 0 : 10
            anchors.centerIn: centerAlign ? parent : undefined
            anchors.verticalCenter: centerAlign ? undefined : parent.verticalCenter
            text: headerText
            color: Theme.palette.textSecondary
            font.weight: Theme.typography.weightBold
            font.pixelSize: Theme.typography.secondaryText
        }
    }
}
