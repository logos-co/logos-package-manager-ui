import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.PackageManagerUi 1.0

import Controls

// One row in the package list. Pure view — takes the row model + position
// and emits intents.
Rectangle {
    id: root

    property var row
    property int rowIndex: -1
    property bool zebra: false

    signal detailsRequested(int index)
    signal selectionToggled(int index, bool checked)
    signal upgradeRequested(int index)
    signal downgradeRequested(int index)
    signal reinstallRequested(int index)
    signal uninstallRequested(int index)

    width: ListView.view ? ListView.view.width : implicitWidth
    height: 35
    color: zebra ? Qt.darker(Theme.palette.surface, 1.15) : Theme.palette.surface

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 40
            Layout.fillHeight: true
            color: "transparent"

            CheckBox {
                id: selectBox
                anchors.centerIn: parent
                property bool isDisabled: (root.row && root.row.isVariantAvailable !== true) ||
                                          ((root.row && (root.row.installStatus | 0)) !== PackageManagerUi.NotInstalled &&
                                           (root.row && (root.row.installStatus | 0)) !== PackageManagerUi.Failed)
                enabled: !isDisabled
                checked: root.row && root.row.isSelected === true
                onCheckedChanged: {
                    if (root.row && checked !== root.row.isSelected) {
                        root.selectionToggled(root.rowIndex, checked)
                    }
                }

                indicator: Rectangle {
                    implicitWidth: 18
                    implicitHeight: 18
                    x: selectBox.leftPadding
                    y: selectBox.height / 2 - height / 2
                    radius: Theme.spacing.radiusSmall
                    color: selectBox.isDisabled
                           ? Theme.palette.backgroundMuted
                           : (selectBox.checked ? Theme.palette.info : Theme.palette.backgroundSecondary)
                    border.color: selectBox.isDisabled
                                  ? Theme.palette.borderSubtle
                                  : (selectBox.checked ? Theme.palette.info : Theme.palette.border)

                    LogosText {
                        anchors.centerIn: parent
                        text: "✓"
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.secondaryText
                        visible: selectBox.checked
                    }
                }
            }
        }

        DataCell { cellText: root.row ? (root.row.name || "") : ""; columnWidth: 150 }
        DataCell { cellText: root.row ? (root.row.type || "") : ""; columnWidth: 80 }
        DataCell { cellText: root.row ? (root.row.description || "") : ""; fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 120
            Layout.fillHeight: true
            color: "transparent"

            StatusBadge {
                modelData: root.row
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle {
            Layout.preferredWidth: 110
            Layout.fillHeight: true
            color: "transparent"

            PackageActionButton {
                anchors.centerIn: parent
                modelData: root.row
                onUpgradeRequested: root.upgradeRequested(root.rowIndex)
                onDowngradeRequested: root.downgradeRequested(root.rowIndex)
                onReinstallRequested: root.reinstallRequested(root.rowIndex)
                onUninstallRequested: root.uninstallRequested(root.rowIndex)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.detailsRequested(root.rowIndex)
        z: -1
    }

    // Reusable data cell local to the row
    component DataCell: Rectangle {
        property string cellText: ""
        property int columnWidth: 80
        property bool fillWidth: false

        Layout.preferredWidth: fillWidth ? -1 : columnWidth
        Layout.fillWidth: fillWidth
        Layout.fillHeight: true
        color: "transparent"

        LogosText {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: fillWidth ? parent.right : undefined
            anchors.rightMargin: fillWidth ? 10 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: cellText
            color: Theme.palette.text
            font.pixelSize: Theme.typography.primaryText
            elide: Text.ElideRight
            width: fillWidth ? undefined : (parent.width - 15)
        }
    }
}
