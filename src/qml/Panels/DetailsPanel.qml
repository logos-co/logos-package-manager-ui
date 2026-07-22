import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.Icons
import Logos.PackageManagerUi 1.0

// Right-side details panel
Rectangle {
    id: root

    property var details: ({})

    signal closeRequested()

    color: Theme.palette.surfaceRaised
    radius: Theme.spacing.radiusXlarge
    clip: true

    QtObject {
        id: d

        function shortHash(h) {
            if (!h) return ""
            if (h.length <= 16) return h
            return h.substring(0, 8) + "…" + h.substring(h.length - 8)
        }

        function formatDetails(detail) {
            if (!detail || !detail.name) return ""
            var header = (detail.displayName && detail.displayName.length > 0)
                         ? detail.displayName : detail.name
            var out = header + "\n\n"

            if (detail.name && detail.name !== header) {
                out += qsTr("Package: %1").arg(detail.name) + "\n"
            }
            if (detail.moduleName && detail.moduleName !== detail.name) {
                out += qsTr("Module Name: %1").arg(detail.moduleName) + "\n"
            }
            out += qsTr("Description: %1")
                .arg(detail.description || qsTr("No description available")) + "\n\n"
            if (detail.type)     out += qsTr("Type: %1").arg(detail.type) + "\n"
            if (detail.category) out += qsTr("Category: %1").arg(detail.category) + "\n"

            var status           = detail.installStatus | 0
            var releaseVersion   = detail.version          || ""
            var releaseHash      = detail.hash             || ""
            var installedVersion = detail.installedVersion || ""
            var installedHash    = detail.installedHash    || ""

            if (status === PackageManagerUi.Installed) {
                out += qsTr("Status: Installed") + "\n"
                if (installedVersion) out += qsTr("Installed version: %1").arg(installedVersion) + "\n"
                if (installedHash)    out += qsTr("Installed hash: %1").arg(d.shortHash(installedHash)) + "\n"
            } else if (status === PackageManagerUi.Installing) {
                out += qsTr("Status: Installing…") + "\n"
            } else if (status === PackageManagerUi.Failed) {
                out += qsTr("Status: Failed") + "\n"
                if (detail.errorMessage) out += qsTr("Error: %1").arg(detail.errorMessage) + "\n"
            } else if (status === PackageManagerUi.UpgradeAvailable
                       || status === PackageManagerUi.DowngradeAvailable
                       || status === PackageManagerUi.DifferentHash) {
                if (status === PackageManagerUi.UpgradeAvailable)
                    out += qsTr("Status: Upgrade Available") + "\n"
                else if (status === PackageManagerUi.DowngradeAvailable)
                    out += qsTr("Status: Downgrade Available") + "\n"
                else
                    out += qsTr("Status: Different Hash") + "\n"
                out += qsTr("Installed: %1 (%2)")
                    .arg(installedVersion).arg(d.shortHash(installedHash)) + "\n"
                out += qsTr("Release:   %1 (%2)")
                    .arg(releaseVersion).arg(d.shortHash(releaseHash)) + "\n"
            } else {
                out += qsTr("Status: Not Installed") + "\n"
                if (releaseVersion) out += qsTr("Release version: %1").arg(releaseVersion) + "\n"
                if (releaseHash)    out += qsTr("Release hash: %1").arg(d.shortHash(releaseHash)) + "\n"
            }

            var deps = detail.dependencies
            if (deps && deps.length > 0) {
                out += "\n" + qsTr("Dependencies:") + "\n"
                for (var i = 0; i < deps.length; i++) out += "  • " + deps[i] + "\n"
            } else {
                out += "\n" + qsTr("Dependencies: None") + "\n"
            }
            return out
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.medium
        spacing: Theme.spacing.small

        // ─── Header: title + close button ───
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small

            LogosText {
                Layout.fillWidth: true
                text: qsTr("Details")
                font.pixelSize: Theme.typography.panelTitleText
                font.weight: Theme.typography.weightMedium
                color: Theme.palette.text
            }

            LogosIconButton {
                iconSource: LogosIcons.close
                size: 28
                iconSize: 14
                iconColor: Theme.palette.textTertiary
                onClicked: root.closeRequested()
            }
        }

        // ─── Body: scrollable formatted text ───
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            LogosText {
                width: parent.width
                text: d.formatDetails(root.details)
                font.pixelSize: Theme.typography.primaryText
                color: Theme.palette.textSecondary
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }
        }
    }
}
