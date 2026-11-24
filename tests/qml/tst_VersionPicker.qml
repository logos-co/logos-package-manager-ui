import QtQuick
import QtTest

import Controls

// A package row's version dropdown, which the table's Available column and
// a package's details both show.
TestCase {
    id: testCase
    name: "VersionPicker"
    width: 400
    height: 300
    visible: true
    when: windowShown

    Component { id: pickerComp; VersionPicker { width: 160 } }
    Component { id: spyComp; SignalSpy {} }

    function make(row) {
        const picker = createTemporaryObject(pickerComp, testCase, { row: row })
        verify(picker)
        return picker
    }

    function test_the_versions_the_row_publishes() {
        const picker = make({
            availableVersions: [{ version: "0.4.0" }, { version: "" }, { version: "0.3.1" }],
            selectedVersionIndex: 1
        })
        compare(picker.versions.length, 2, "one without a version is no choice")
        compare(picker.model, ["0.4.0", "0.3.1"])
        compare(picker.currentIndex, 1)
        compare(picker.displayText, "0.3.1")
    }

    function test_none_to_pick() {
        compare(make({ availableVersions: [{ version: "" }] }).versions.length, 0)
        compare(make({}).versions.length, 0)
        compare(make(null).versions.length, 0)
    }

    // Re-releases of one version read with their dates and hashes; closed,
    // the box shows the version alone.
    function test_a_rerelease_reads_with_its_date_and_hash() {
        const picker = make({
            availableVersions: [
                { version: "1.0.0", releasedAt: "2026-10-09T10:00:00Z", rootHash: "ab12cd34ef56aa" },
                { version: "1.0.0", releasedAt: "2026-10-01T10:00:00Z", rootHash: "77ee" }
            ],
            selectedVersionIndex: 0
        })
        compare(picker.model, ["1.0.0 · 2026-10-09 · ab12cd…", "1.0.0 · 2026-10-01 · 77ee"])
        compare(picker.displayText, "1.0.0")
    }

    function test_picking_another_version_asks_for_it() {
        const picker = make({
            availableVersions: [{ version: "0.4.0" }, { version: "0.3.1" }],
            selectedVersionIndex: 0
        })
        const picked = createTemporaryObject(spyComp, testCase, { target: picker, signalName: "versionPicked" })
        picker.activated(1)
        compare(picked.count, 1)
        compare(picked.signalArguments[0][0], 1)
        picker.activated(0)
        compare(picked.count, 1, "the one it is on is no change")
    }
}
