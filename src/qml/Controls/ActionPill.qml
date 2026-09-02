import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls
import Logos.PackageManagerUi 1.0

// Per-row primary-action pill. Replaces the read-only StatusBadge: this
// IS the action button, not a label next to one. Reads `rowAction` from
// the model (the value PackageListModel computed against the row's
// SELECTED dropdown version), maps to a label + colour family, and on
// click emits `actionRequested(rowAction)` so the caller can dispatch
// the matching backend slot.
//
// Two states are computed independently of `rowAction`:
//   * `installStatus === Installing` → the pill labels itself
//     "Installing…" and disables, regardless of what action would
//     otherwise be runnable. The transient state isn't a RowAction
//     value (see PackageTypes::RowAction comment).
//   * `rowAction === NotAvailable` → disabled, tooltip carries the
//     specific notAvailableReason.
//
// `NoOp` is the terminal "installed and matches selected" state — non-
// clickable, label "Installed", muted styling so it doesn't draw the
// eye away from the runnable rows.

Control {
    id: root

    objectName: "pmui.ActionPill"

    required property var modelData

    signal actionRequested(int rowAction)

    readonly property int action: modelData ? (modelData.rowAction | 0)
                                             : PackageManagerUi.NoOp
    readonly property int installStatus: modelData ? (modelData.installStatus | 0) : 0
    readonly property bool installing: installStatus === PackageManagerUi.Installing
    readonly property bool runnable: !installing
                                      && action !== PackageManagerUi.NoOp
                                      && action !== PackageManagerUi.NotAvailable
    readonly property real downloadReceived: modelData ? (modelData.downloadReceived || 0) : 0
    readonly property real downloadTotal: modelData ? (modelData.downloadTotal || 0) : 0
    readonly property bool showProgress: installing && downloadTotal > 0
                                          && downloadReceived < downloadTotal
    readonly property bool indeterminateProgress: installing && downloadTotal <= 0

    QtObject {
        id: d

        // Mirrors PackageList's size column formatting so the pill and the
        // Size cell describe the same bytes the same way.
        function formatBytes(n) {
            if (!isFinite(n) || n <= 0) return "0 B"
            if (n < 1024) return Math.round(n) + " B"
            if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
            if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB"
            return (n / (1024 * 1024 * 1024)).toFixed(1) + " GB"
        }

        function actionText(a, installing) {
            // Counting bytes only while they're actually moving. Once the
            // transfer completes the remaining work (verify, install) has no
            // byte count, so the label goes back to the plain state rather
            // than parking on "45.2 / 45.2 MB".
            if (root.showProgress) {
                return qsTr("%1 / %2").arg(d.formatBytes(root.downloadReceived))
                                      .arg(d.formatBytes(root.downloadTotal))
            }
            if (installing) return qsTr("Installing…")
            switch (a) {
            case PackageManagerUi.Install:      return qsTr("Install")
            case PackageManagerUi.Upgrade:      return qsTr("Upgrade")
            case PackageManagerUi.Downgrade:    return qsTr("Downgrade")
            case PackageManagerUi.Reinstall:    return qsTr("Reinstall")
            case PackageManagerUi.Retry:        return qsTr("Retry")
            case PackageManagerUi.NotAvailable: return qsTr("Not available")
            default:                            return qsTr("Installed")  // NoOp
            }
        }

        function baseColor(a, installing) {
            if (installing)                         return Theme.palette.warning
            switch (a) {
            case PackageManagerUi.Install:      return Theme.palette.primary
            case PackageManagerUi.Upgrade:      return Theme.palette.info
            case PackageManagerUi.Downgrade:    return Theme.palette.info
            case PackageManagerUi.Reinstall:    return Theme.palette.warning
            case PackageManagerUi.Retry:        return Theme.palette.error
            case PackageManagerUi.NotAvailable: return Theme.palette.textMuted
            default:                            return Theme.palette.textTertiary  // NoOp
            }
        }

        function notAvailableTooltip(r) {
            if (!r) return ""
            var reason = r.notAvailableReason | 0
            if (reason === PackageManagerUi.NoVariantsPublished)
                return qsTr("No installable build is published for this package.")
            if (reason === PackageManagerUi.BuildFlavorMismatch)
                return qsTr("Not available for this build flavor (dev / portable / release).")
            if (reason === PackageManagerUi.PlatformMismatch)
                return qsTr("Not available for this platform.")
            return qsTr("Not available")
        }

        // Tooltip body. Failed rows show the captured errorMessage; the
        // failed text itself sits in the pill via "Retry", so the
        // tooltip carries the why. NotAvailable rows explain the reason.
        function tooltipText(r, a) {
            if (!r) return ""
            if (a === PackageManagerUi.NotAvailable)
                return d.notAvailableTooltip(r)
            if (a === PackageManagerUi.Retry)
                return r.errorMessage || ""
            return ""
        }
    }

    enabled: runnable
    hoverEnabled: runnable

    // Click → emit the resolved action; caller routes to the matching
    // backend slot via BackendStore.runRowAction().
    Action {
        id: clickAction
        enabled: root.runnable
        onTriggered: root.actionRequested(root.action)
    }

    implicitWidth: root.showProgress ? 116 : badge.implicitWidth

    contentItem: LogosBadge {
        id: badge
        text: d.actionText(root.action, root.installing)
        color: root.runnable
               ? d.baseColor(root.action, root.installing)
               : Theme.palette.textTertiary
        backgroundColor: root.runnable
                         ? Theme.colors.getColor(d.baseColor(root.action, root.installing), 0.18)
                         : Theme.palette.backgroundButton
        borderColor: root.runnable
                     ? d.baseColor(root.action, root.installing)
                     : Theme.palette.backgroundButton
        radius: Theme.spacing.radiusLarge

        implicitHeight: 22
        verticalPadding: 4
        labelItem.font.pixelSize: 11
        labelItem.lineHeight: 12
        labelItem.lineHeightMode: Text.FixedHeight

        LogosProgressBar {
            parent: badge.backgroundItem
            visible: root.showProgress || root.indeterminateProgress
            anchors.fill: parent ? parent : undefined
            anchors.margins: badge.borderWidth

            from: 0
            to: root.downloadTotal > 0 ? root.downloadTotal : 1
            value: root.downloadReceived
            indeterminate: root.indeterminateProgress

            trackColor: "transparent"
            fillColor: Theme.colors.getColor(Theme.palette.warning, 0.45)
            fillRadius: Math.max(0, badge.radius - badge.borderWidth)

            // Smooth the ~200 ms gap between samples so the bar glides.
            Behavior on value {
                NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
            }
        }

        // The badge is the visible click target — wire its MouseArea
        // through the Action rather than letting the Control's default
        // pointer handling fire so we get keyboard/Enter support too.
        MouseArea {
            anchors.fill: parent
            enabled: root.runnable
            cursorShape: Qt.PointingHandCursor
            onClicked: clickAction.trigger()
        }
    }

    LogosToolTip {
        text: d.tooltipText(root.modelData, root.action)
        placement: LogosToolTip.Top
        visible: root.hovered && text !== ""
    }
}
