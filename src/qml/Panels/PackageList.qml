import QtQuick
import QtQuick.Controls

import Logos.Theme
import Logos.Controls
import Logos.Icons
import Logos.PackageManagerUi 1.0

import Controls

// Package list rendered as a LogosTable.
LogosTable {
    id: root

    property var packagesModel

    signal detailsRequested(int index)
    signal selectionToggled(int index, bool checked)
    // Kebab ⋮ menu items. The pill drives `actionRequested`; the kebab
    // owns the secondary affordances (Reload, View details, Uninstall).
    signal reloadRequested(int index)
    signal uninstallRequested(int index)
    // Per-row primary action click. `action` is a PackageTypes::RowAction
    // value (Install / Upgrade / Downgrade / Reinstall / Retry). Parent
    // routes via `BackendStore.runRowAction(index, action)`.
    signal actionRequested(int index, int action)
    // Per-row Version dropdown — emitted when the user picks a different
    // version from the cell ComboBox. Parent wires this to
    // backend.setRowVersion.
    signal versionChanged(int index, int versionIndex)

    model: root.packagesModel
    selectionMode: LogosTable.Multi

    onRowClicked: function(idx, row) { root.detailsRequested(idx) }

    function clearSelections() {
        root.selectedIndices = []
        d.previousSelection = []
    }

    QtObject {
        id: d
        property var previousSelection: []

        function emitDiff() {
            const prev = d.previousSelection
            const next = root.selectedIndices
            const prevSet = new Set(prev)
            const nextSet = new Set(next)
            for (const i of next)
                if (!prevSet.has(i)) root.selectionToggled(i, true)
            for (const i of prev)
                if (!nextSet.has(i)) root.selectionToggled(i, false)
            d.previousSelection = next.slice()
        }

        function formatSize(bytes) {
            const n = Number(bytes)
            if (!isFinite(n) || n <= 0) return "—"
            if (n < 1024) return n + " B"
            if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
            if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB"
            return (n / (1024 * 1024 * 1024)).toFixed(1) + " GB"
        }

        function formatDate(value) {
            if (!value) return "—"
            const date = new Date(value)
            if (isNaN(date.getTime())) return "—"
            return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
        }

        function newestVersionString(r) {
            if (!r) return ""
            const av = r.availableVersions || []
            if (av.length === 0) return ""
            const first = av[0]
            return (first && first.version) ? String(first.version) : ""
        }

        // Gate the kebab's Uninstall item. Mirrors PackageListModel's
        // isUninstallableRow predicate: must be user-installed AND in a
        // state where uninstall is meaningful.
        function canUninstall(r) {
            if (!r) return false
            if (r.installType !== "user") return false
            const s = r.installStatus | 0
            return s === PackageManagerUi.Installed
                || s === PackageManagerUi.UpgradeAvailable
                || s === PackageManagerUi.DowngradeAvailable
                || s === PackageManagerUi.DifferentHash
        }

        function canReload(r) {
            if (!r) return false
            return r.isVariantAvailable === true
                && (r.installStatus | 0) === PackageManagerUi.Installed
        }
    }

    onSelectionChanged: d.emitDiff()

    columns: [
        LogosTableColumn {
            title: qsTr("Package")
            role: "name"
            minWidth: 200
            preferredWidth: 220
            sortable: true
        },
        // Source-by-row column intentionally absent — the row delegate
        // draws a section header for each source group (Logos Official
        // first, then user repos alphabetically) so we don't repeat
        // the source label on every row.
        LogosTableColumn {
            title: qsTr("Type")
            role: "type"
            minWidth: 80
            preferredWidth: 90
            sortable: true
        },
        LogosTableColumn {
            // Per-row Version dropdown. Populated from
            // rowItem.availableVersions (date-sorted, newest first); the
            // currently selected entry mirrors into VersionRole on
            // PackageListModel so status/details bindings reflect the pick.
            // A small ▲ marker is overlaid when rowItem.updateAvailable
            // is true (installed < newest catalog) — independent of the
            // dropdown pick, so a Reinstall of the installed version
            // still surfaces "a newer one exists".
            title: qsTr("Version")
            role: "version"
            minWidth: 140
            preferredWidth: 160
            cellDelegate: versionCellComponent
        },
        LogosTableColumn {
            // Per-row primary action — resolved against the user's
            // SELECTED dropdown version, not the catalog newest. Drops
            // the static Status badge that used to lie when the user
            // moved the dropdown. The cell itself IS the click target.
            title: qsTr("Action")
            role: "rowAction"
            minWidth: 130
            preferredWidth: 140
            cellDelegate: actionPillComponent
        },
        LogosTableColumn {
            title: qsTr("Description")
            role: "description"
            minWidth: 220
            preferredWidth: 240
            fillWidth: true
        },
        LogosTableColumn {
            // Kebab ⋮ — View details / Reload / Uninstall. Uninstall is
            // intentionally only reachable here (destructive, no bulk
            // surface), gated by installType === "user".
            title: ""
            minWidth: 44
            preferredWidth: 44
            alignment: Qt.AlignHCenter | Qt.AlignVCenter
            cellDelegate: moreButtonComponent
        }
    ]

    Component {
        id: actionPillComponent
        Item {
            ActionPill {
                anchors.centerIn: parent
                modelData: rowItem
                onActionRequested: function(action) {
                    root.actionRequested(rowIndex, action)
                }
            }
        }
    }

    // Group rows by source via ListView's section feature. The backend
    // pre-sorts packages by (sourcePriority, sourceName, packageName)
    // — Logos Official first, then user repos alpha — so iterating in
    // model order yields contiguous source runs. Section property is
    // `repositoryDisplayName`; the delegate below renders a header
    // strip above each group with the source label on the left and a
    // count chip on the right.
    Component.onCompleted: {
        if (view) {
            view.section.property = "repositoryDisplayName"
            view.section.criteria = ViewSection.FullString
            view.section.delegate = sectionHeaderDelegate
        }
    }

    Component {
        id: sectionHeaderDelegate
        Rectangle {
            // `section` is the QML-injected property carrying the
            // group's value (the repositoryDisplayName string).
            required property string section
            width: ListView.view ? ListView.view.width : 0
            height: 32
            color: Theme.palette.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.palette.border
            }

            LogosText {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                // Empty string falls back to "(unresolved)" so a
                // freshly-added repo whose logos-repo.json hasn't
                // resolved yet still gets a visible header instead of
                // a mystery blank bar.
                text: parent.section.length > 0 ? parent.section : qsTr("(unresolved repository)")
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
                font.weight: Theme.typography.weightBold
            }
        }
    }

    Component {
        // Per-row Version dropdown + update marker.
        //
        // The dropdown drives the row's `selectedVersionIndex`, which
        // PackageListModel::setRowVersion uses to recompute `rowAction`
        // — so the ActionPill in the neighbouring cell flips between
        // Upgrade / Downgrade / Reinstall / NoOp as the user moves it.
        //
        // The update marker (▲) is bound to `rowItem.updateAvailable`,
        // computed once at row-build time against the catalog newest.
        // It stays put regardless of the dropdown pick, so a user
        // choosing "Reinstall" on the installed version still sees that
        // a newer one exists.
        id: versionCellComponent
        Item {
            id: versionCell

            // Filter availableVersions to entries that actually carry a
            // version string. An empty version is useless to surface as
            // a dropdown choice (and shows up as a blank "..." entry
            // when the catalog row predates the new schema), so we
            // treat the cell as "no usable versions" and fall back to
            // the legacy plain-text VersionRole instead.
            property var usableVersions: {
                const raw = rowItem ? (rowItem.availableVersions || []) : []
                const out = []
                for (let i = 0; i < raw.length; ++i) {
                    const v = raw[i]
                    if (v && v.version && String(v.version).length > 0) out.push(v)
                }
                return out
            }
            property int selectedIdx: rowItem && rowItem.selectedVersionIndex !== undefined
                                      ? rowItem.selectedVersionIndex : 0
            property bool hasDuplicateVersions: {
                if (!usableVersions || usableVersions.length < 2) return false
                var seen = {}
                for (var i = 0; i < usableVersions.length; ++i) {
                    var v = usableVersions[i].version
                    if (seen[v]) return true
                    seen[v] = true
                }
                return false
            }
            property bool updateAvailable: rowItem && rowItem.updateAvailable === true

            LogosComboBox {
                id: versionCombo
                anchors.left: parent.left
                anchors.right: updateMarker.visible ? updateMarker.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                visible: versionCell.usableVersions.length > 0
                model: versionCell.usableVersions.map(function(v) {
                    if (!versionCell.hasDuplicateVersions) return v.version || ""
                    // Disambiguate same-version re-releases by date +
                    // short hash so the user can pick the right one.
                    var date = (v.releasedAt || "").substring(0, 10)
                    var sh = (v.rootHash || "").length > 12
                             ? (v.rootHash.substring(0, 6) + "…")
                             : (v.rootHash || "")
                    return (v.version || "") + " · " + date + " · " + sh
                })
                currentIndex: versionCell.selectedIdx
                // Override the closed-state label so it shows just the
                // version string even when the dropdown items carry the
                // verbose "version · date · hash" disambiguator. Without
                // this the long disambiguator gets elided in the narrow
                // cell down to a bare "…", which is what makes every
                // version dropdown look identical at a glance.
                displayText: {
                    const v = versionCell.usableVersions[currentIndex]
                    return v && v.version ? String(v.version) : ""
                }
                onActivated: function(idx) {
                    if (idx !== versionCell.selectedIdx)
                        root.versionChanged(rowIndex, idx)
                }
            }

            // Update-available marker. Renders as a small filled ▲
            // anchored to the right edge of the cell. Independent of
            // the dropdown — present whenever the catalog's newest
            // version is strictly newer than what's installed.
            Item {
                id: updateMarker
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 6
                width: 16
                height: 16
                visible: versionCell.updateAvailable
                         && versionCell.usableVersions.length > 0

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.colors.getColor(Theme.palette.info, 0.18)
                    border.color: Theme.palette.info
                    border.width: 1
                }

                LogosText {
                    anchors.centerIn: parent
                    text: "▲"
                    color: Theme.palette.info
                    font.pixelSize: 9
                    font.weight: Theme.typography.weightBold
                }

                MouseArea {
                    id: markerHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.ArrowCursor
                }

                LogosToolTip {
                    visible: markerHover.containsMouse
                    placement: LogosToolTip.Top
                    text: {
                        if (!rowItem) return ""
                        const installed = rowItem.installedVersion
                                          ? String(rowItem.installedVersion) : ""
                        const newest = d.newestVersionString(rowItem)
                        if (installed && newest)
                            return qsTr("v%1 installed · v%2 available").arg(installed).arg(newest)
                        return qsTr("Update available")
                    }
                }
            }

            // Fallback plain text when no usable versions[] (legacy
            // rows, pre-fetch state, or index entries without manifest
            // contents). Shows the row's VersionRole — set to the
            // current selection's version by setRowVersion, or to the
            // catalog row's `version` field otherwise — so the column
            // is never an empty "..." dropdown.
            LogosText {
                anchors.fill: parent
                anchors.margins: 8
                visible: versionCell.usableVersions.length === 0
                text: {
                    if (!rowItem) return ""
                    const v = rowItem.version
                    return (v && String(v).length > 0) ? String(v) : "—"
                }
                color: Theme.palette.textMuted
                font.pixelSize: Theme.typography.secondaryText
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    Component {
        // Kebab ⋮ — View details / Reload / Uninstall. Uninstall is the
        // ONLY surface for the destructive op now: no bulk header
        // button, no per-row trash icon. Gating mirrors the model's
        // isUninstallableRow / isInstalled predicates so this never
        // offers Uninstall on an embedded module.
        id: moreButtonComponent
        Item {
            id: moreCell
            LogosIconButton {
                id: moreBtn
                anchors.centerIn: parent
                size: 32
                iconSize: 18
                iconSource: LogosIcons.more
                background: Item {}
                onClicked: rowMenu.open()
            }
            Menu {
                id: rowMenu
                // Anchor to the icon so the menu drops directly below.
                x: moreBtn.x
                y: moreBtn.y + moreBtn.height

                MenuItem {
                    text: qsTr("View details")
                    onTriggered: root.detailsRequested(rowIndex)
                }
                MenuItem {
                    text: qsTr("Reload")
                    enabled: d.canReload(rowItem)
                    onTriggered: root.reloadRequested(rowIndex)
                }
                MenuItem {
                    text: qsTr("Uninstall")
                    enabled: d.canUninstall(rowItem)
                    onTriggered: root.uninstallRequested(rowIndex)
                }
            }
        }
    }
}
