import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.Icons
import Logos.PackageManagerUi 1.0

// Per-row action pill: [Reload] [Install ⇄ Uninstall]. 
//
// Reload is currently not implemented perhaps to be removed
//
// TODO: Upgrade / Downgrade / DifferentHash currently fall through to
// the uninstall icon (the package is installed; just out of date). They
// should grow dedicated icons + actions once that flow is wired per-row.
Control {
    id: root

    required property var modelData

    // TODO: Flip to true once PackageManagerBackend::reloadPackage actually
    // calls into logoscore.
    property bool reloadReady: false

    signal reloadRequested()
    signal installRequested()
    signal uninstallRequested()

    QtObject {
        id: d

        readonly property bool variantAvailable: !!root.modelData
                                                 && root.modelData.isVariantAvailable === true
        readonly property int status: root.modelData ? (root.modelData.installStatus | 0) : -1

        readonly property bool isInstalled: d.status === PackageManagerUi.Installed
                                            || d.status === PackageManagerUi.UpgradeAvailable
                                            || d.status === PackageManagerUi.DowngradeAvailable
                                            || d.status === PackageManagerUi.DifferentHash

        readonly property bool canUninstall: d.isInstalled
                                             && root.modelData
                                             && root.modelData.installType === "user"

        readonly property bool reloadEnabled: root.reloadReady
                                              && d.variantAvailable
                                              && d.status === PackageManagerUi.Installed

        readonly property string reloadDisabledReason: {
            if (d.reloadEnabled) return "";
            if (!root.reloadReady)
                return qsTr("Reload not yet supported.");
            if (!d.variantAvailable)
                return qsTr("No variant available for this platform.");
            if (d.status !== PackageManagerUi.Installed)
                return qsTr("Install the package before reloading.");
            return "";
        }

        readonly property bool secondaryEnabled: d.isInstalled
                                                 ? d.canUninstall
                                                 : (d.variantAvailable
                                                    && d.status !== PackageManagerUi.Installing)

        readonly property string secondaryDisabledReason: {
            if (d.secondaryEnabled) return "";
            if (d.isInstalled) {
                return qsTr("Embedded module — cannot be uninstalled.");
            }
            if (d.status === PackageManagerUi.Installing)
                return qsTr("Installation already in progress.");
            if (!d.variantAvailable)
                return qsTr("No variant available for this platform.");
            return "";
        }
    }

    implicitWidth: 110
    implicitHeight: 52
    leftPadding: 12
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0

    background: Rectangle {
        color: Theme.colors.getColor(Theme.palette.backgroundInset, 0.6)
        radius: Theme.spacing.radiusLarge
    }

    contentItem: RowLayout {
        spacing: 8

        LogosIconButton {
            id: reloadBtn
            Layout.alignment: Qt.AlignVCenter
            iconSource: LogosIcons.refresh
            enabled: d.reloadEnabled

            LogosToolTip {
                text: d.reloadDisabledReason
                placement: LogosToolTip.Top
                visible: reloadBtn.hovered && d.reloadDisabledReason !== ""
            }

            background: Rectangle {
                color: Theme.palette.backgroundButton
                radius: Theme.spacing.radiusPill
                border.color: Theme.palette.borderTertiaryMuted
                border.width: 1
            }
            onClicked: root.reloadRequested()
        }

        LogosIconButton {
            id: secondaryBtn
            Layout.alignment: Qt.AlignVCenter
            iconSource: d.isInstalled ? LogosIcons.trash : LogosIcons.install
            enabled: d.secondaryEnabled

            LogosToolTip {
                text: d.secondaryDisabledReason
                placement: LogosToolTip.Top
                visible: secondaryBtn.hovered
                         && d.secondaryDisabledReason !== ""
            }

            background: Rectangle {
                color: d.isInstalled
                       ? Theme.colors.getColor(Theme.palette.backgroundButton, 0.54)
                       : Theme.palette.backgroundButton
                radius: Theme.spacing.radiusPill
                border.color: Theme.palette.borderTertiaryMuted
                border.width: 1
            }
            
            onClicked: d.isInstalled ? root.uninstallRequested()
                                     : root.installRequested()
        }
    }
}
