import QtQuick

import Logos.Theme
import Logos.Controls
import Logos.PackageManagerUi 1.0

// Pill rendered in the package list's Status column.
// Consumer supplies the row model from the delegate.
Rectangle {
    id: root

    required property var modelData

    property QtObject d: QtObject {
        id: d

        function _baseColor(r) {
            if (!r || r.isVariantAvailable !== true) return Theme.palette.warning
            var s = r.installStatus | 0
            if (s === PackageManagerUi.Installing) return Theme.palette.warning
            if (s === PackageManagerUi.Installed) return Theme.palette.success
            if (s === PackageManagerUi.Failed) return Theme.palette.error
            if (s === PackageManagerUi.UpgradeAvailable) return Theme.palette.info
            if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.accentBurntOrange
            if (s === PackageManagerUi.DifferentHash) return Theme.palette.accentOrangeMid
            return Theme.palette.textTertiary
        }

        function _textColor(r) {
            if (!r || r.isVariantAvailable !== true) return Theme.palette.warningHover
            var s = r.installStatus | 0
            if (s === PackageManagerUi.Installing) return Theme.palette.warningHover
            if (s === PackageManagerUi.Installed) return Theme.palette.successHover
            if (s === PackageManagerUi.Failed) return Theme.palette.errorHover
            if (s === PackageManagerUi.UpgradeAvailable) return Theme.palette.info
            if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.accentOrangeMid
            if (s === PackageManagerUi.DifferentHash) return Theme.palette.accentOrange
            return Theme.palette.textSecondary
        }

        function _text(r) {
            if (!r || r.isVariantAvailable !== true) return qsTr("Not Available")
            var s = r.installStatus | 0
            if (s === PackageManagerUi.Installing) return qsTr("Installing…")
            if (s === PackageManagerUi.Installed) return qsTr("Installed")
            if (s === PackageManagerUi.Failed) return qsTr("Failed")
            if (s === PackageManagerUi.UpgradeAvailable) return qsTr("Upgrade")
            if (s === PackageManagerUi.DowngradeAvailable) return qsTr("Downgrade")
            if (s === PackageManagerUi.DifferentHash) return qsTr("Different Hash")
            return qsTr("Not Installed")
        }
    }

    implicitWidth: 100
    implicitHeight: 20
    radius: Theme.spacing.radiusSmall
    color: Theme.colors.getColor(d._baseColor(root.modelData), 0.18)
    border.color: d._baseColor(root.modelData)
    border.width: 1

    LogosText {
        anchors.centerIn: parent
        text: d._text(root.modelData)
        color: d._textColor(root.modelData)
        font.pixelSize: Theme.typography.secondaryText
    }
}
