import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

// Per-row dep-confirm popup.
//
// Fires AFTER the user clicks Install / Reinstall / Upgrade / Downgrade
// on a row, IFF the resolver surfaced transitive changes that need
// action (deps that are missing or whose installed version doesn't
// satisfy the new manifest's range). The no-changes path proceeds
// silently — this dialog never opens for the "everything already
// satisfied" case.
//
// Three explicit outcomes. The dialog routes each through a distinct
// backend slot keyed on the catalog `packageName`:
//   * confirmedWithDeps(packageName)
//     → `BackendStore.confirmInstallWithDeps(packageName)`
//     The action runs as the user requested, AND every transitive
//     change the dialog listed is applied.
//   * confirmedWithoutDeps(packageName)
//     → `BackendStore.confirmInstallWithoutDeps(packageName)`
//     The action runs but transitive changes are SKIPPED. The package
//     might fail to load if any of those changes was actually required;
//     the user explicitly chose this trade-off.
//   * cancelled(packageName)
//     → `BackendStore.cancelInstallConfirm(packageName)`
//     Backend drops the pending entry; nothing was mutated yet so
//     cancel is "as if you never clicked".
//
// Inputs (set by the caller's open* shim):
//   packageName / displayName — identity (key + label)
//   actionLabel               — "Install" | "Reinstall" | "Upgrade" | "Downgrade"
//   fromVersion               — installed version (empty for fresh install)
//   toVersion                 — target version
//   depChanges                — [{ name, action: "install"|"upgrade"|"downgrade",
//                                  fromVersion, toVersion, repository }, ...]
//                               (the actual list the user is being asked about)

Popup {
    id: root

    // Opaque key identifying the backend's pending request. Echoed back
    // verbatim in the three signals so the backend drains the right
    // entry — package name alone isn't unique across repos.
    property string requestKey: ""
    property string packageName: ""
    property string displayName: ""
    property string actionLabel: "Install"
    property string fromVersion: ""
    property string toVersion: ""
    property var    depChanges: []

    signal confirmedWithDeps(string requestKey)
    signal confirmedWithoutDeps(string requestKey)
    signal cancelled(string requestKey)

    // Convenience entry point so callers don't have to set the props by
    // hand — matches the openWith* pattern the basecamp
    // ConfirmationDialog uses.
    function openWith(payload) {
        root.requestKey   = (payload && payload.requestKey)   || ""
        root.packageName  = (payload && payload.packageName)  || ""
        root.displayName  = (payload && payload.displayName)  || root.packageName
        root.actionLabel  = (payload && payload.actionLabel)  || "Install"
        root.fromVersion  = (payload && payload.fromVersion)  || ""
        root.toVersion    = (payload && payload.toVersion)    || ""
        root.depChanges   = (payload && payload.depChanges)   || []
        root._explicitClose = false
        open()
    }

    // Set true in the confirm/cancel button handlers so the onClosed
    // hook doesn't double-fire `cancelled` when Escape lands after a
    // button click. Mirrors ConfirmationDialog's pattern.
    property bool _explicitClose: false

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: Theme.spacing.large
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.palette.background
        border.color: Theme.palette.border
        border.width: 1
        radius: 8
    }

    QtObject {
        id: d

        // "v1.0.0 → v1.0.1" / "v1.0.0" / "" — same helper the bulk
        // RunActionsConfirm popup uses.
        function versionTransition(item) {
            if (!item) return ""
            var to   = item.toVersion ? "v" + item.toVersion : ""
            var from = item.fromVersion ? "v" + item.fromVersion : ""
            if (from && to && from !== to) return from + " → " + to
            if (to) return to
            return ""
        }

        // Title verb uses actionLabel directly so the dialog matches
        // the per-row ActionPill the user just clicked. qsTr template
        // with placeholders so translators control word order / quoting.
        function titleText() {
            var ver = root.toVersion ? " v" + root.toVersion : ""
            return qsTr("%1 '%2'%3?").arg(root.actionLabel)
                                     .arg(root.displayName)
                                     .arg(ver)
        }

        // Body lead-in. qsTr with a plural-aware template — the count and
        // dependency/dependencies noun are placeholders so the string is
        // translatable as a whole rather than concatenated fragments.
        function bodyText() {
            var n = (root.depChanges || []).length
            if (n === 0)
                // Plain single-package confirm — no transitive changes. The
                // title already names the package + version being installed.
                return qsTr("No other packages need to change.")
            return n === 1
                ? qsTr("This %1 requires changes to 1 dependency that isn't already on disk in a compatible version.")
                      .arg(root.actionLabel.toLowerCase())
                : qsTr("This %1 requires changes to %2 dependencies that aren't already on disk in a compatible version.")
                      .arg(root.actionLabel.toLowerCase()).arg(n)
        }

        // Action verb cap'd for inline display in the per-row line.
        function verbFor(action) {
            if (!action) return ""
            return action.charAt(0).toUpperCase() + action.slice(1)
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing.medium

        LogosText {
            Layout.fillWidth: true
            text: d.titleText()
            font.pixelSize: Theme.typography.titleText
            font.weight: Theme.typography.weightBold
            color: Theme.palette.text
            wrapMode: Text.WordWrap
        }

        LogosText {
            Layout.fillWidth: true
            text: d.bodyText()
            font.pixelSize: Theme.typography.primaryText
            color: Theme.palette.textSecondary
            wrapMode: Text.WordWrap
        }

        // Dep list — boxed for visual grouping. Each row is a
        // two-column grid: action label | "name (repo)  vA → vB".
        // No bullet character; the action label IS the leading column.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.tiny
            color: Theme.palette.surface
            border.color: Theme.palette.border
            border.width: 1
            radius: 4
            visible: (root.depChanges || []).length > 0
            implicitHeight: depList.implicitHeight + 2 * Theme.spacing.small

            ListView {
                id: depList
                anchors.fill: parent
                anchors.margins: Theme.spacing.small
                model: root.depChanges
                spacing: 2
                clip: true
                interactive: contentHeight > height
                implicitHeight: Math.min(180,
                    Math.max(contentHeight, 0))

                delegate: RowLayout {
                    width: ListView.view ? ListView.view.width : 0
                    spacing: Theme.spacing.medium

                    // Action chip — fixed-width column so the names
                    // line up vertically across multiple rows. Color
                    // hints at semantics (install/upgrade/downgrade)
                    // without needing a legend.
                    Rectangle {
                        Layout.preferredWidth: 76
                        Layout.preferredHeight: 22
                        radius: 11
                        color: {
                            switch (modelData.action) {
                            case "install":   return Theme.colors.getColor(Theme.palette.success, 0.18)
                            case "upgrade":   return Theme.colors.getColor(Theme.palette.info,    0.18)
                            case "downgrade": return Theme.colors.getColor(Theme.palette.warning, 0.18)
                            default:          return Theme.colors.getColor(Theme.palette.info,    0.18)
                            }
                        }
                        LogosText {
                            anchors.centerIn: parent
                            text: d.verbFor(modelData.action || "")
                            color: {
                                switch (modelData.action) {
                                case "install":   return Theme.palette.success
                                case "upgrade":   return Theme.palette.info
                                case "downgrade": return Theme.palette.warning
                                default:          return Theme.palette.text
                                }
                            }
                            font.pixelSize: Theme.typography.secondaryText
                            font.weight: Theme.typography.weightMedium
                        }
                    }

                    // Name + version line. The repo, when known, is
                    // suffixed in parentheses; single-source catalogs
                    // skip it cleanly.
                    LogosText {
                        Layout.fillWidth: true
                        text: {
                            var name = modelData.name || ""
                            var repo = modelData.repository || ""
                            var ver  = d.versionTransition(modelData)
                            var head = repo ? (name + " (" + repo + ")") : name
                            return ver ? (head + "  " + ver) : head
                        }
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.primaryText
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        // Three-button footer, stacked vertically. Stacking avoids the
        // horizontal-overflow trap (Cancel + two action labels + the
        // dynamic package name was easily 500+ px); it also lets each
        // button label stay readable on long package names. Primary
        // action on top so the keyboard's default-button cycle hits it
        // first.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.small
            spacing: Theme.spacing.small

            LogosButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: Theme.spacing.radiusLarge
                // With no dep changes, the with/without-deps split is
                // meaningless — a single plain "<action>" confirms the install.
                text: (root.depChanges || []).length > 0
                      ? qsTr("Install with dependencies")
                      : root.actionLabel
                onClicked: {
                    root._explicitClose = true
                    root.confirmedWithDeps(root.requestKey)
                    root.close()
                }
            }
            LogosButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: Theme.spacing.radiusLarge
                // Redundant when there are no transitive changes.
                visible: (root.depChanges || []).length > 0
                text: qsTr("Install just '%1'").arg(root.displayName)
                onClicked: {
                    root._explicitClose = true
                    root.confirmedWithoutDeps(root.requestKey)
                    root.close()
                }
            }
            LogosButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: Theme.spacing.radiusLarge
                text: qsTr("Cancel")
                onClicked: {
                    root._explicitClose = true
                    root.cancelled(root.requestKey)
                    root.close()
                }
            }
        }
    }

    onClosed: {
        // Escape (or any other Dialog-managed dismissal) maps to
        // Cancel so the backend's pending entry is always drained.
        // Button clicks set _explicitClose first to avoid double-firing.
        if (root._explicitClose) {
            root._explicitClose = false
            return
        }
        root.cancelled(root.requestKey)
    }
}
