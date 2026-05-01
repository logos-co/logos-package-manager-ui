import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls
import Logos.PackageManagerUi 1.0

// Per-row action button: Upgrade/Downgrade/Reinstall/Uninstall, or hidden.
Button {
    id: root

    required property var modelData

    signal upgradeRequested()
    signal downgradeRequested()
    signal reinstallRequested()
    signal uninstallRequested()

    property QtObject d: QtObject {
        id: d

        readonly property string label: d._labelFor(root.modelData)
        // Drive uninstall styling from the row enum, not the (translatable)
        // label string — string comparison would break under qsTr() in
        // non-English locales.
        readonly property bool isUninstall: root.modelData
                                            && (root.modelData.installStatus | 0) === PackageManagerUi.Installed
                                            && root.modelData.installType === "user"

        function _labelFor(r) {
            if (!r) return ""
            var s = r.installStatus | 0
            if (s === PackageManagerUi.UpgradeAvailable) return qsTr("Upgrade")
            if (s === PackageManagerUi.DowngradeAvailable) return qsTr("Downgrade")
            if (s === PackageManagerUi.DifferentHash) return qsTr("Reinstall")
            if (s === PackageManagerUi.Installed && r.installType === "user") return qsTr("Uninstall")
            return ""
        }
    }

    visible: d.label !== ""
    text: d.label
    onClicked: {
        if (!modelData) return
        var s = modelData.installStatus | 0
        if (s === PackageManagerUi.UpgradeAvailable) root.upgradeRequested()
        else if (s === PackageManagerUi.DowngradeAvailable) root.downgradeRequested()
        else if (s === PackageManagerUi.DifferentHash) root.reinstallRequested()
        else if (s === PackageManagerUi.Installed && modelData.installType === "user") root.uninstallRequested()
    }

    contentItem: LogosText {
        text: root.text
        font.pixelSize: Theme.typography.secondaryText
        color: root.enabled ? Theme.palette.text : Theme.palette.textMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 90
        implicitHeight: 22
        radius: Theme.spacing.radiusSmall
        color: root.enabled
               ? (d.isUninstall
                    ? (root.pressed ? Theme.palette.errorPressed : Theme.palette.error)
                    : (root.pressed ? Theme.palette.successPressed : Theme.palette.success))
               : Theme.palette.backgroundSecondary
        border.color: root.enabled
                      ? (d.isUninstall ? Theme.palette.errorHover : Theme.palette.successHover)
                      : Theme.palette.borderSubtle
        border.width: 1
    }
}
