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

    required property var modelData

    signal actionRequested(int rowAction)

    readonly property int _action: modelData ? (modelData.rowAction | 0)
                                             : PackageManagerUi.NoOp
    readonly property int _installStatus: modelData ? (modelData.installStatus | 0) : 0
    readonly property bool _installing: _installStatus === PackageManagerUi.Installing
    readonly property bool _runnable: !_installing
                                      && _action !== PackageManagerUi.NoOp
                                      && _action !== PackageManagerUi.NotAvailable

    QtObject {
        id: d

        function actionText(a, installing) {
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

    enabled: _runnable
    hoverEnabled: _runnable

    // Click → emit the resolved action; caller routes to the matching
    // backend slot via BackendStore.runRowAction().
    Action {
        id: clickAction
        enabled: root._runnable
        onTriggered: root.actionRequested(root._action)
    }

    contentItem: LogosBadge {
        id: badge
        text: d.actionText(root._action, root._installing)
        color: root._runnable
               ? d.baseColor(root._action, root._installing)
               : Theme.palette.textTertiary
        backgroundColor: root._runnable
                         ? Theme.colors.getColor(d.baseColor(root._action, root._installing), 0.18)
                         : Theme.palette.backgroundButton
        borderColor: root._runnable
                     ? d.baseColor(root._action, root._installing)
                     : Theme.palette.backgroundButton
        radius: Theme.spacing.radiusLarge

        implicitHeight: 22
        verticalPadding: 4
        labelItem.font.pixelSize: 11
        labelItem.lineHeight: 12
        labelItem.lineHeightMode: Text.FixedHeight

        // The badge is the visible click target — wire its MouseArea
        // through the Action rather than letting the Control's default
        // pointer handling fire so we get keyboard/Enter support too.
        MouseArea {
            anchors.fill: parent
            enabled: root._runnable
            cursorShape: Qt.PointingHandCursor
            onClicked: clickAction.trigger()
        }
    }

    LogosToolTip {
        text: d.tooltipText(root.modelData, root._action)
        placement: LogosToolTip.Top
        visible: root.hovered && text !== ""
    }
}
