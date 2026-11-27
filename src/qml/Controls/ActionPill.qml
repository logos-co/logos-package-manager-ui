import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls
import Logos.Icons
import Logos.PackageManagerUi 1.0

import "../Installer/Availability.js" as Availability
import "../Installer/Downloads.js" as Downloads
import "../Installer/Stages.js" as Stages

// Per-row primary-action pill. Replaces the read-only StatusBadge: this
// IS the action button, not a label next to one. Reads `rowAction` from
// the model (computed against the row's SELECTED dropdown version), maps
// to a label + colour family, and on click emits
// `actionRequested(rowAction)` so the caller can dispatch the matching
// backend slot. The words are the shared installer words (Installer/).
//
// Two states are computed independently of `rowAction`:
//   * `installStatus === Installing` → the pill names the stage (Queued,
//     Finding peers…, Downloading…, bytes, Installing…) and stops being
//     clickable, regardless of what action would otherwise be runnable.
//     The transient state isn't a RowAction value.
//   * `rowAction === NotAvailable` → not clickable, tooltip carries the
//     specific notAvailableReason.
//
// `NoOp` is the terminal "installed and matches selected" state — non-
// clickable, label "Installed", muted styling so it doesn't draw the
// eye away from the runnable rows.
//
// "Non-clickable" is enforced by `runnable` on the Action and the badge's
// MouseArea, NOT by Item.enabled — see the hoverEnabled comment below.

Control {
    id: root

    objectName: "pmui.ActionPill"

    required property var modelData

    signal actionRequested(int rowAction)

    readonly property int action: modelData ? (modelData.rowAction | 0)
                                             : PackageManagerUi.NoOp
    readonly property int installStatus: modelData ? (modelData.installStatus | 0) : 0
    readonly property bool installing: installStatus === PackageManagerUi.Installing
    // Waiting behind another install: nothing of its own has started.
    readonly property bool queued: installing && !!modelData && modelData.queued === true
    readonly property bool runnable: !installing
                                      && action !== PackageManagerUi.NoOp
                                      && action !== PackageManagerUi.NotAvailable
    readonly property real downloadReceived: modelData ? (modelData.downloadReceived || 0) : 0
    readonly property real downloadTotal: modelData ? (modelData.downloadTotal || 0) : 0
    readonly property bool downloadDone: downloadTotal > 0
                                         && downloadReceived >= downloadTotal
    readonly property bool showProgress: installing
                                         && Downloads.transferring(downloadReceived, downloadTotal)
    // "logos", "http", or "" until this package's own download begins.
    readonly property string transport: installing && modelData
                                        ? (modelData.downloadTransport || "") : ""
    // Where the bytes come from, at the start of the pill. Only once the
    // downloader has said: while waiting, the Source column shows the one
    // it will try first.
    readonly property string transportIconName: Downloads.transportIconName(transport)
    readonly property url transportIcon: transportIconName ? LogosIcons[transportIconName] : ""
    readonly property bool indeterminateProgress:
        installing && !downloadDone && (downloadReceived <= 0 || downloadTotal <= 0)
    // The tooltip's text, for a place that shows it without hovering: what
    // clicking would change, or why it can't run.
    readonly property string helpText: d.tooltipText(modelData, action)

    QtObject {
        id: d

        function actionText(a, installing) {
            // While installing, the stage: the bytes while they move, then
            // what comes after them, which has no byte count.
            if (installing)
                return Stages.progressWord({ queued: root.queued, transport: root.transport,
                                             received: root.downloadReceived,
                                             total: root.downloadTotal })
            // Verb only. The version it would install is one hover away in
            // the tooltip, and the Installed / Available columns carry the
            // pair inline — putting it on the pill too was a third copy.
            return Stages.actionWord(a)
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

        // Tooltip body. Failed rows show the captured errorMessage; the
        // failed text itself sits in the pill via "Retry", so the
        // tooltip carries the why. NotAvailable rows explain the reason.
        // Everything else spells out the version transition — "Upgrade"
        // alone never said from what, to what. `version` follows the row's
        // Version dropdown, so this tracks the pick rather than the newest.
        function tooltipText(r, a) {
            if (!r) return ""
            if (a === PackageManagerUi.NotAvailable)
                return Availability.notAvailableText(r.notAvailableReason | 0)
            const transition = Stages.transitionText(r.installedVersion, r.version)
            if (a === PackageManagerUi.Retry)
                return r.errorMessage || transition
            return transition
        }
    }

    hoverEnabled: true

    // Click → emit the resolved action; caller routes to the matching
    // backend slot via BackendStore.runRowAction().
    Action {
        id: clickAction
        enabled: root.runnable
        onTriggered: root.actionRequested(root.action)
    }

    // MEASURED, not guessed: a hardcoded number was wrong for the font and
    // wider than anything actually rendered, so clicking Install visibly
    // over-stretched the pill. This is the exact width of the widest label
    // at the badge's own font, so the pill is as tight as the content allows.
    TextMetrics {
        id: widestInstallLabel
        font: badge.labelItem.font
        text: Downloads.widestProgressLabel
    }
    // The badge's icon is 12px with 4px spacing.
    readonly property int installingWidth:
        Math.ceil(widestInstallLabel.width) + badge.leftPadding + badge.rightPadding
        + (root.transportIcon.toString().length > 0 ? 16 : 0)
    implicitWidth: root.installing ? installingWidth : badge.implicitWidth

    // Ease the one remaining step — natural width to installingWidth on
    // click, and back when it finishes — so it glides instead of snapping.
    Behavior on implicitWidth {
        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
    }

    contentItem: LogosBadge {
        id: badge
        text: d.actionText(root.action, root.installing)
        iconSource: root.transportIcon
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
        labelItem.lineHeight: 12
        labelItem.lineHeightMode: Text.FixedHeight

        LogosProgressBar {
            parent: badge.backgroundItem
            visible: root.showProgress || root.indeterminateProgress
            anchors.left: parent ? parent.left : undefined
            anchors.right: parent ? parent.right : undefined
            anchors.bottom: parent ? parent.bottom : undefined
            anchors.margins: badge.borderWidth + 1
            height: 3

            from: 0
            to: root.downloadTotal > 0 ? root.downloadTotal : 1
            value: root.downloadReceived
            indeterminate: root.indeterminateProgress

            trackColor: "transparent"
            fillColor: Theme.colors.getColor(Theme.palette.warning, 0.95)

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
        text: root.helpText
        placement: LogosToolTip.Top
        visible: root.hovered && text !== ""
    }
}
