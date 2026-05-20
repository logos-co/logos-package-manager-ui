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
// the user sees the per-action breakdown ("Install 2 · Upgrade 1 ·
// Reinstall 1") and explicitly confirms before anything runs. Uninstall
// is intentionally NOT a category here: it's per-row only.
//
// Inputs: `summary` is a QVariantMap with non-zero counts only — keys
// are "install" / "upgrade" / "downgrade" / "reinstall" / "retry",
// matching PackageActionPlan::toSummary() in PackageListModel.cpp.
// Anything else is ignored.
//
// Emits `confirmed()` when the user clicks Confirm; the caller is then
// responsible for calling `BackendStore.runSelectedActions()`.

Popup {
    id: root

    // Set by the caller before open() — typically bound to
    // `store.actionSummary` at the call site.
    property var summary: ({})

    signal confirmed()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 380
    padding: Theme.spacing.medium

    background: Rectangle {
        color: Theme.palette.background
        border.color: Theme.palette.border
        border.width: 1
        radius: 6
    }

    QtObject {
        id: d

        // The fixed display order. Listed actions that aren't in
        // `summary` (or are zero) get skipped — the visible list only
        // contains real, non-zero rows. Retry comes last because it's
        // the rarest and visually de-emphasised by ordering.
        readonly property var order: ["install", "upgrade", "downgrade", "reinstall", "retry"]

        function label(key, n) {
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

        // Summary lines — one per non-zero action.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small

            Repeater {
                model: d.order.filter(function(k) {
                    var v = root.summary ? root.summary[k] : 0
                    return Number(v) > 0
                })

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small

                    LogosText {
                        text: "•"
                        color: Theme.palette.textSecondary
                        font.pixelSize: Theme.typography.bodyText
                    }
                    LogosText {
                        Layout.fillWidth: true
                        text: d.label(modelData,
                              Number(root.summary[modelData] || 0))
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.bodyText
                        wrapMode: Text.WordWrap
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
