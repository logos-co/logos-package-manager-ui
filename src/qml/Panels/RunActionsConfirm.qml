import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

// Confirm-summary popup for the "Run Actions" header button.
//
// Why this exists: the previous two-button (Install + Uninstall)
// arrangement let a mixed selection silently fire each button on its
// eligible subset, so the user had no clear preview of what would
// actually happen. The new "Run Actions" path goes through this popup —
// the user sees the per-action category header AND the specific
// per-row transitions ("wallet_module: v1.0.0 → v1.0.1") underneath,
// and explicitly confirms before anything runs. Uninstall is
// intentionally NOT a category here: it's per-row only.
//
// Inputs:
//   `summary` — QVariantMap from PackageActionPlan::toSummary, non-zero
//               counts only ({install: N, upgrade: N, ...}). Drives the
//               category section headers.
//   `items`   — QVariantList from PackageActionPlan::toItemList, one
//               entry per selected runnable row in model order. Each
//               entry has { name, displayName, action, repository,
//               fromVersion, toVersion }. Grouped by `action` so each
//               summary header is followed by the matching rows.
//
// Emits `confirmed()` when the user clicks Confirm; the caller is then
// responsible for calling `BackendStore.runSelectedActions()`.

Popup {
    id: root

    // Set by the caller before open() — typically bound to
    // `store.actionSummary` / `store.actionPlanItems` at the call site.
    property var summary: ({})
    property var items: []

    signal confirmed()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: Theme.spacing.medium

    background: Rectangle {
        color: Theme.palette.background
        border.color: Theme.palette.border
        border.width: 1
        radius: 6
    }

    QtObject {
        id: d

        // Fixed display order. Listed actions that aren't in `summary`
        // (or are zero) get skipped — the visible list only contains
        // real, non-zero rows. Retry comes last because it's the rarest
        // and visually de-emphasised by ordering.
        readonly property var order: ["install", "upgrade", "downgrade", "reinstall", "retry"]

        // Filter on the fly so the Repeater binding stays declarative.
        // Re-computed when items/summary changes (both are var props).
        function itemsForAction(action) {
            var list = root.items || []
            var out = []
            for (var i = 0; i < list.length; ++i) {
                if (list[i] && list[i].action === action) out.push(list[i])
            }
            return out
        }

        function categoryLabel(key, n) {
            switch (key) {
            case "install":   return n === 1 ? qsTr("Install 1 package")
                                             : qsTr("Install %1 packages").arg(n)
            case "upgrade":   return n === 1 ? qsTr("Upgrade 1 package")
                                             : qsTr("Upgrade %1 packages").arg(n)
            case "downgrade": return n === 1 ? qsTr("Downgrade 1 package")
                                             : qsTr("Downgrade %1 packages").arg(n)
            case "reinstall": return n === 1 ? qsTr("Reinstall 1 package")
                                             : qsTr("Reinstall %1 packages").arg(n)
            case "retry":     return n === 1 ? qsTr("Retry 1 failed install")
                                             : qsTr("Retry %1 failed installs").arg(n)
            default: return ""
            }
        }

        // "v1.0.0 → v1.0.1" for an upgrade/downgrade/reinstall;
        // "v1.0.0" alone for a fresh install (no prior version);
        // empty string when neither version is known.
        function versionTransition(it) {
            if (!it) return ""
            var to   = it.toVersion ? "v" + it.toVersion : ""
            var from = it.fromVersion ? "v" + it.fromVersion : ""
            if (from && to && from !== to) return from + " → " + to
            if (to) return to
            return ""
        }

        function totalCount(s) {
            if (!s) return 0
            var t = 0
            for (var k in s) {
                var v = Number(s[k])
                if (isFinite(v)) t += v
            }
            return t
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing.medium

        LogosText {
            Layout.fillWidth: true
            text: qsTr("Run actions on %1 selected row(s)?")
                  .arg(d.totalCount(root.summary))
            font.pixelSize: Theme.typography.heading
            font.weight: Theme.typography.weightBold
            color: Theme.palette.text
            wrapMode: Text.WordWrap
        }

        // One section per non-zero action category. Section = header
        // line + indented bullet list of the rows in that category.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.medium

            Repeater {
                model: d.order.filter(function(k) {
                    var v = root.summary ? root.summary[k] : 0
                    return Number(v) > 0
                })

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.tiny

                    // Cached property — Repeater delegates re-evaluate
                    // their bindings when `model`/`root.items` changes
                    // because the binding reads them.
                    readonly property var rowsInSection: d.itemsForAction(modelData)

                    // Category header ("Upgrade 2 packages")
                    LogosText {
                        Layout.fillWidth: true
                        text: d.categoryLabel(modelData,
                              Number(root.summary[modelData] || 0))
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.bodyText
                        font.weight: Theme.typography.weightBold
                        wrapMode: Text.WordWrap
                    }

                    // Per-row detail — bullets sit under the header
                    // with a hanging indent so the eye groups them
                    // with the category, not with the next category.
                    Repeater {
                        model: parent.rowsInSection

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.spacing.medium
                            spacing: Theme.spacing.small

                            LogosText {
                                text: "•"
                                color: Theme.palette.textSecondary
                                font.pixelSize: Theme.typography.bodyText
                            }
                            LogosText {
                                Layout.fillWidth: true
                                text: {
                                    var name = modelData.displayName || modelData.name || ""
                                    var ver  = d.versionTransition(modelData)
                                    var repo = modelData.repository || ""
                                    // "name (repo): v1.0.0 → v1.0.1"
                                    // — repo is suffixed only when set,
                                    // so single-source catalogs don't
                                    // get cluttered.
                                    var head = repo ? (name + " (" + repo + ")") : name
                                    return ver ? (head + ": " + ver) : head
                                }
                                color: Theme.palette.text
                                font.pixelSize: Theme.typography.bodyText
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }

        // Footer buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.small
            spacing: Theme.spacing.medium

            Item { Layout.fillWidth: true }

            LogosButton {
                text: qsTr("Cancel")
                Layout.preferredHeight: 36
                Layout.preferredWidth: 100
                radius: Theme.spacing.radiusLarge
                onClicked: root.close()
            }

            LogosButton {
                text: qsTr("Confirm")
                Layout.preferredHeight: 36
                Layout.preferredWidth: 110
                radius: Theme.spacing.radiusLarge
                enabled: d.totalCount(root.summary) > 0
                onClicked: {
                    root.confirmed()
                    root.close()
                }
            }
        }
    }
}
