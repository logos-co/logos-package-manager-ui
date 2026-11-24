import QtQuick

import Logos.Theme
import Logos.Controls

// A package row's version dropdown: the versions its catalog publishes, on the
// one the row has selected. Picking one emits versionPicked; the backend's
// setRowVersion then moves the row's `selectedVersionIndex`, recomputes its
// `rowAction` (so its ActionPill flips between Upgrade / Downgrade /
// Reinstall / NoOp) and mirrors the pick's size and release date onto it.
LogosComboBox {
    id: root

    objectName: "pmui.VersionPicker"

    required property var row

    // The row's versions that carry a version string. An empty one is no
    // choice (a row that predates the schema shows it as a blank "…"); with
    // none left there is nothing to pick, and the caller shows the row's
    // plain version instead.
    readonly property var versions: {
        const raw = row ? (row.availableVersions || []) : []
        const out = []
        for (let i = 0; i < raw.length; ++i) {
            const v = raw[i]
            if (v && v.version && String(v.version).length > 0) out.push(v)
        }
        return out
    }

    signal versionPicked(int versionIndex)

    QtObject {
        id: d

        readonly property int selectedIndex: root.row && root.row.selectedVersionIndex !== undefined
                                             ? root.row.selectedVersionIndex : 0
        readonly property bool hasDuplicateVersions: {
            const seen = {}
            for (let i = 0; i < root.versions.length; ++i) {
                const v = root.versions[i].version
                if (seen[v]) return true
                seen[v] = true
            }
            return false
        }
    }

    model: versions.map(function(v) {
        if (!d.hasDuplicateVersions) return v.version || ""
        // Same-version re-releases, told apart by date and short hash.
        const date = (v.releasedAt || "").substring(0, 10)
        const hash = (v.rootHash || "").length > 12
                     ? (v.rootHash.substring(0, 6) + "…")
                     : (v.rootHash || "")
        return (v.version || "") + " · " + date + " · " + hash
    })
    currentIndex: d.selectedIndex
    // Closed, just the version: the "version · date · hash" entries would
    // elide to a bare "…" in a narrow box.
    displayText: {
        const v = versions[currentIndex]
        return v && v.version ? String(v.version) : ""
    }
    hoverEnabled: true

    onActivated: function(index) {
        if (index !== d.selectedIndex)
            root.versionPicked(index)
    }

    LogosToolTip {
        text: root.displayText
        placement: LogosToolTip.Top
        visible: root.hovered && root.contentLabel.truncated
    }
}
