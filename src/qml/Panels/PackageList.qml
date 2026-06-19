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
    // Per-row Uninstall — fired from the trash icon in the trailing
    // cell. View Details fires from a plain row click (above). Reload
    // is intentionally unsurfaced (the slot is a TODO stub); revive
    // this signal + a UI affordance for it once the backend implements
    // load/unload via logoscore.
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
    // Bulk-action surface (checkbox column + Run Actions button) is
    // intentionally off — per-row ActionPill is the single act path.
    // LogosTable.None drops the auto-prepended checkbox column
    // entirely; emitDiff / rowSelectable / selectionToggled below are
    // kept compiled but never fire as long as selectionMode stays None.
    selectionMode: LogosTable.None

    onRowClicked: function(idx, row) { root.detailsRequested(idx) }

    function clearSelections() {
        root.selectedIndices = []
        d.previousSelection = []
    }

    QtObject {
        id: d
        property var previousSelection: []
        // Cached role int for "rowAction"; populated lazily on the
        // first query since the model isn't bound yet at component
        // construction time. -1 means "not resolved yet / no model".
        property int _rowActionRoleId: -1

        // Map the model's "rowAction" role name to its int id so the
        // emitDiff filter below can read the action without spinning
        // up a hidden delegate per row. roleNames() returns the
        // {intRoleId -> "name"} mapping the model declared.
        function _resolveRowActionRole() {
            if (d._rowActionRoleId >= 0) return d._rowActionRoleId
            if (!root.model || !root.model.roleNames) return -1
            const names = root.model.roleNames()
            for (const k in names) {
                // `names[k]` is a ByteArray in Qt; coercing to string
                // for comparison is safe on every Qt 6 we support.
                if (String(names[k]) === "rowAction") {
                    d._rowActionRoleId = parseInt(k)
                    return d._rowActionRoleId
                }
            }
            return -1
        }

        // True iff the row at proxy-index i is in a runnable RowAction
        // state (Install / Upgrade / Downgrade / Reinstall / Retry).
        // Non-runnable rows (NoOp = "Installed", NotAvailable) are
        // dropped from the selection so the bulk Run Actions plan
        // never includes a row that would no-op.
        function isRunnableIdx(i) {
            const r = d._resolveRowActionRole()
            if (r < 0 || !root.model) return true   // fail open if we can't tell
            const idx = root.model.index(i, 0)
            if (!idx || !idx.valid) return true
            const a = (Number(root.model.data(idx, r)) | 0)
            return a !== PackageManagerUi.NoOp
                && a !== PackageManagerUi.NotAvailable
        }

        function emitDiff() {
            const prev = d.previousSelection
            const raw = root.selectedIndices
            // NOTE: bulk selection is currently OFF (selectionMode:
            // LogosTable.None above), so the table renders no checkbox
            // column and this path is dormant — kept intact for when the
            // bulk "Run Actions" surface is re-enabled. When it is, also
            // re-add a LogosTable.rowSelectable binding so non-runnable
            // rows can't be ticked in the first place; this post-hoc
            // filter is then the belt-and-braces sweep for a row that was
            // ticked while runnable and later flipped to NoOp/NotAvailable
            // via a dropdown change.
            const cleaned = raw.filter(d.isRunnableIdx)
            if (cleaned.length !== raw.length) {
                root.selectedIndices = cleaned
                // Setting selectedIndices re-fires onSelectionChanged
                // → this function re-enters with raw === cleaned and
                // emits the real toggles. Bail out of THIS pass.
                return
            }

            const prevSet = new Set(prev)
            const nextSet = new Set(cleaned)
            for (const i of cleaned)
                if (!prevSet.has(i)) root.selectionToggled(i, true)
            for (const i of prev)
                if (!nextSet.has(i)) root.selectionToggled(i, false)
            d.previousSelection = cleaned.slice()
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

    }

    onSelectionChanged: d.emitDiff()

    columns: [
        LogosTableColumn {
            title: qsTr("Package")
            role: "displayName"
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
            title: qsTr("Size")
            role: "size"
            minWidth: 80
            preferredWidth: 90
            sortable: true
            cellDelegate: sizeCellComponent
        },
        LogosTableColumn {
            title: qsTr("Date Updated")
            role: "dateUpdated"
            minWidth: 110
            preferredWidth: 120
            sortable: true
            cellDelegate: dateCellComponent
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
            // Sized for a fixed-width combo (~90px, fits "1.0.10") + the
            // marker slot to its right, so cells line up vertically and
            // the marker presence never shrinks the combo. See
            // versionCellComponent's inline width constants.
            minWidth: 120
            preferredWidth: 130
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
            // Per-row Uninstall (trash icon). Renders only when the
            // row is user-installed; embedded modules and not-installed
            // rows leave the cell blank. Uninstall is intentionally
            // kept out of the bulk Run Actions plan (destructive), so
            // this icon is the only surface for the operation.
            title: ""
            minWidth: 44
            preferredWidth: 44
            alignment: Qt.AlignHCenter | Qt.AlignVCenter
            cellDelegate: uninstallButtonComponent
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

    Component {
        id: sizeCellComponent
        LogosText {
            anchors.fill: parent
            text: rowItem ? d.formatSize(rowItem.size) : ""
            color: Theme.palette.text
            font.pixelSize: Theme.typography.primaryText
            horizontalAlignment: (columnDef.alignment & Qt.AlignHCenter) ? Text.AlignHCenter
                               : (columnDef.alignment & Qt.AlignRight)   ? Text.AlignRight
                               : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    Component {
        id: dateCellComponent
        LogosText {
            anchors.fill: parent
            text: rowItem ? d.formatDate(rowItem.dateUpdated) : ""
            color: Theme.palette.text
            font.pixelSize: Theme.typography.primaryText
            horizontalAlignment: (columnDef.alignment & Qt.AlignHCenter) ? Text.AlignHCenter
                               : (columnDef.alignment & Qt.AlignRight)   ? Text.AlignRight
                               : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
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
                // Fixed width so marker presence/absence doesn't reflow
                // the combo, and so all rows render the same dropdown
                // size regardless of cell width. 92px holds "1.0.10"
                // comfortably after subtracting the chevron padding —
                // longer prerelease strings (e.g. "1.0.0-rc.1") will
                // elide, which the displayText fallback below already
                // collapses to just the version number anyway.
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
                width: 92
                height: Math.min(parent.height - 8, 32)
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
            // anchored to the right of the fixed-width combo so the
            // combo's size stays the same whether or not the marker is
            // visible (the user complained about the combo shrinking on
            // rows that had the marker — that's why this is anchored to
            // versionCombo.right rather than to the cell's right edge).
            // Independent of the dropdown pick — present whenever the
            // catalog's newest version is strictly newer than installed.
            Item {
                id: updateMarker
                anchors.left: versionCombo.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
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
        // Per-row Uninstall trash icon. Only visible when canUninstall
        // (user-installed, not embedded, in an Installed/Upgrade/
        // Downgrade/DifferentHash state) — for everything else the
        // cell renders blank, keeping the row visually unambiguous
        // about whether the action is reachable.
        id: uninstallButtonComponent
        Item {
            visible: d.canUninstall(rowItem)
            LogosIconButton {
                anchors.centerIn: parent
                size: 32
                iconSize: 18
                iconSource: LogosIcons.trash
                background: Item {}
                onClicked: root.uninstallRequested(rowIndex)
                LogosToolTip {
                    text: qsTr("Uninstall")
                    placement: LogosToolTip.Top
                    visible: parent.hovered
                }
            }
        }
    }
}
