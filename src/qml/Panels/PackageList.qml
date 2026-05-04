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
            preferredWidth: 200
            sortable: true
        },
        LogosTableColumn {
            title: qsTr("Type")
            role: "type"
            minWidth: 90
            preferredWidth: 90
            sortable: true
        },
        LogosTableColumn {
            title: qsTr("Description")
            role: "description"
            minWidth: 220
            preferredWidth: 240
            fillWidth: true
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
