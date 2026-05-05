import QtQuick

import Logos.Theme
import Logos.Controls
import Logos.PackageManagerUi 1.0

LogosBadge {
    id: root

    required property var modelData

    property QtObject d: QtObject {
        id: d

        function baseColor(r) {
            if (!r || r.isVariantAvailable !== true) return Theme.palette.textMuted
            var s = r.installStatus | 0
            if (s === PackageManagerUi.Installing)         return Theme.palette.warning
            if (s === PackageManagerUi.Installed)          return Theme.palette.primary
            if (s === PackageManagerUi.Failed)             return Theme.palette.error
            if (s === PackageManagerUi.UpgradeAvailable)   return Theme.palette.info
            if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.info
            if (s === PackageManagerUi.DifferentHash)      return Theme.palette.warning
            return Theme.palette.textTertiary
        }

        function isSolidBg(r) {
            if (!r || r.isVariantAvailable !== true) return true
            var s = r.installStatus | 0
            return s === PackageManagerUi.NotInstalled
        }

        function bgColor(r) {
            return isSolidBg(r)
                ? Theme.palette.backgroundButton
                : Theme.colors.getColor(baseColor(r), 0.18)
        }

        function borderColor(r) {
            // Solid-bg states have no visible border (border = bg);
            // tinted-bg states use the family color as the contrasting border.
            return isSolidBg(r) ? Theme.palette.backgroundButton : baseColor(r)
        }

        function textColor(r) {
            if (!r || r.isVariantAvailable !== true) return Theme.palette.textTertiary
            var s = r.installStatus | 0
            if (s === PackageManagerUi.Installing)         return Theme.palette.warningHover
            if (s === PackageManagerUi.Installed)          return Theme.palette.primary
            if (s === PackageManagerUi.Failed)             return Theme.palette.errorHover
            if (s === PackageManagerUi.UpgradeAvailable)   return Theme.palette.info
            if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.info
            if (s === PackageManagerUi.DifferentHash)      return Theme.palette.warningHover
            return Theme.palette.textTertiary
        }

        function text(r) {
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

    text: d.text(root.modelData)
    color: d.textColor(root.modelData)
    backgroundColor: d.bgColor(root.modelData)
    borderColor: d.borderColor(root.modelData)
    radius: Theme.spacing.radiusLarge

    implicitHeight: 16
    verticalPadding: 2
    labelItem.font.pixelSize: 11
    labelItem.lineHeight: 12
    labelItem.lineHeightMode: Text.FixedHeight
}
