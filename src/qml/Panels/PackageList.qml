import QtQuick

import Logos.Theme
import Logos.Controls
import Logos.Icons

import Controls

// Package list rendered as a LogosTable.
LogosTable {
    id: root

    property var packagesModel

    signal detailsRequested(int index)
    signal selectionToggled(int index, bool checked)
    signal reloadRequested(int index)
    signal installRequested(int index)
    signal uninstallRequested(int index)
    signal moreRequested(int index)
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
            // Width is wider than a pure semver needs because the
            // dropdown items can include a date+short-hash disambiguator
            // when two releases share the same `version` string; without
            // the headroom the items elide to a bare "…" and every row
            // looks identical ("aliased" in user-speak).
            title: qsTr("Version")
            role: "version"
            minWidth: 130
            preferredWidth: 150
            cellDelegate: versionCellComponent
        },
        LogosTableColumn {
            title: qsTr("Description")
            role: "description"
            minWidth: 220
            preferredWidth: 240
            fillWidth: true
        },
        //LogosTableColumn {
        //    title: qsTr("Size")
        //    role: "size"
        //    minWidth: 80
        //    preferredWidth: 90
        //    sortable: true
        //    cellDelegate: sizeCellComponent
        //},
        //LogosTableColumn {
        //    title: qsTr("Date Updated")
        //    role: "dateUpdated"
        //    minWidth: 110
        //    preferredWidth: 120
        //    sortable: true
        //    cellDelegate: dateCellComponent
        //},
        LogosTableColumn {
            title: qsTr("Status")
            role: "installStatus"
            minWidth: 130
            preferredWidth: 140
            cellDelegate: statusBadgeComponent
        },
        LogosTableColumn {
            title: qsTr("Actions")
            minWidth: 110
            preferredWidth: 110
            alignment: Qt.AlignHCenter | Qt.AlignVCenter
            cellDelegate: actionButtonComponent
        },
        LogosTableColumn {
            title: ""
            minWidth: 44
            preferredWidth: 44
            alignment: Qt.AlignHCenter | Qt.AlignVCenter
            cellDelegate: moreButtonComponent
        }
    ]

    Component {
        id: sizeCellComponent
        LogosText {
            text: rowItem ? d.formatSize(rowItem.size) : ""
            color: Theme.palette.textMuted
            font.pixelSize: Theme.typography.secondaryText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    Component {
        id: dateCellComponent
        LogosText {
            text: rowItem ? d.formatDate(rowItem.dateUpdated) : ""
            color: Theme.palette.textMuted
            font.pixelSize: Theme.typography.secondaryText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    Component {
        id: statusBadgeComponent
        Item {
            StatusBadge {
                anchors.centerIn: parent
                modelData: rowItem
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
        // Per-row Version dropdown. ComboBox.model maps each version
        // entry to a label; the QML scope shadowing trap (ComboBox's
        // own `model` shadows the row context) is sidestepped by
        // capturing `rowItem.availableVersions` onto the wrapping Item.
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

            LogosComboBox {
                id: versionCombo
                anchors.fill: parent
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
        id: actionButtonComponent
        Item {
            PackageActionButton {
                anchors.centerIn: parent
                modelData: rowItem
                onReloadRequested: root.reloadRequested(rowIndex)
                onInstallRequested: root.installRequested(rowIndex)
                onUninstallRequested: root.uninstallRequested(rowIndex)
            }
        }
    }

    Component {
        id: moreButtonComponent
        Item {
            LogosIconButton {
                anchors.centerIn: parent
                size: 32
                iconSize: 18
                iconSource: LogosIcons.more
                background: Item {}
                onClicked: root.moreRequested(rowIndex)
            }
        }
    }
}
