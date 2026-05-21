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

    property string packageName: ""
    property string displayName: ""
    property string actionLabel: "Install"
    property string fromVersion: ""
    property string toVersion: ""
    property var    depChanges: []

    signal confirmedWithDeps(string packageName)
    signal confirmedWithoutDeps(string packageName)
    signal cancelled(string packageName)

    // Convenience entry point so callers don't have to set seven props
    // by hand — matches the openWith* pattern the basecamp
    // ConfirmationDialog uses.
    function openWith(payload) {
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
    width: 480
    padding: Theme.spacing.medium
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.palette.background
        border.color: Theme.palette.border
        border.width: 1
        radius: 6
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

        // Title verb — uses actionLabel directly so the dialog matches
        // the per-row ActionPill label the user just clicked.
        function titleText() {
            var ver = root.toVersion ? " v" + root.toVersion : ""
            return root.actionLabel + " '" + root.displayName + "'" + ver + "?"
        }

        // Body lead-in — explains why we're asking. The deps list
        // renders below.
        function bodyText() {
            var n = (root.depChanges || []).length
            var noun = n === 1 ? "dependency" : "dependencies"
            return "This " + root.actionLabel.toLowerCase()
                 + " requires changes to " + n + " " + noun
                 + " that aren't already on disk in a compatible version."
                 + " You can apply all of them, install only '"
                 + root.displayName + "' on its own, or cancel."
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing.medium

        LogosText {
            Layout.fillWidth: true
            text: d.titleText()
            font.pixelSize: Theme.typography.heading
            font.weight: Theme.typography.weightBold
            color: Theme.palette.text
            wrapMode: Text.WordWrap
        }

        LogosText {
            Layout.fillWidth: true
            text: d.bodyText()
            font.pixelSize: Theme.typography.bodyText
            color: Theme.palette.textSecondary
            wrapMode: Text.WordWrap
        }

        // Dep list — one line per transitive change. Boxed for visual
        // grouping the same way ConfirmationDialog's cascade list is.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180,
                Math.max(36, (root.depChanges || []).length * 28))
            color: Theme.palette.surface
            radius: 4
            border.color: Theme.palette.border
            border.width: 1
            visible: (root.depChanges || []).length > 0

            ListView {
                anchors.fill: parent
                anchors.margins: 8
                model: root.depChanges
                clip: true

                delegate: RowLayout {
                    width: ListView.view ? ListView.view.width : 0
                    spacing: Theme.spacing.small

                    LogosText {
                        text: "•"
                        color: Theme.palette.textSecondary
                        font.pixelSize: Theme.typography.bodyText
                    }
                    LogosText {
                        Layout.fillWidth: true
                        text: {
                            var name = (modelData.name || "")
                            var ver  = d.versionTransition(modelData)
                            var repo = modelData.repository || ""
                            var head = repo ? (name + " (" + repo + ")") : name
                            var act  = modelData.action || ""
                            // "wallet_ui (Logos Official): install v1.2.0"
                            // "chat_module: upgrade v1.0.0 → v1.1.0"
                            var verb = act ? (act.charAt(0).toUpperCase() + act.slice(1)) : ""
                            return ver
                                ? (head + ": " + (verb ? verb + " " : "") + ver)
                                : (head + (verb ? ": " + verb : ""))
                        }
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.bodyText
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Three-button footer. Confirm-with-deps is the safe default
        // (left-most non-cancel slot, primary colour). "Just the
        // package" is intentionally less prominent — explicit opt-in.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.small
            spacing: Theme.spacing.medium

            LogosButton {
                text: qsTr("Cancel")
                Layout.preferredHeight: 36
                Layout.preferredWidth: 96
                radius: Theme.spacing.radiusLarge
                onClicked: {
                    root._explicitClose = true
                    root.cancelled(root.packageName)
                    root.close()
                }
            }
            Item { Layout.fillWidth: true }
            LogosButton {
                text: qsTr("Install just '%1'").arg(root.displayName)
                Layout.preferredHeight: 36
                Layout.preferredWidth: 200
                radius: Theme.spacing.radiusLarge
                onClicked: {
                    root._explicitClose = true
                    root.confirmedWithoutDeps(root.packageName)
                    root.close()
                }
            }
            LogosButton {
                text: qsTr("Install with dependencies")
                Layout.preferredHeight: 36
                Layout.preferredWidth: 210
                radius: Theme.spacing.radiusLarge
                onClicked: {
                    root._explicitClose = true
                    root.confirmedWithDeps(root.packageName)
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
        root.cancelled(root.packageName)
    }
}
