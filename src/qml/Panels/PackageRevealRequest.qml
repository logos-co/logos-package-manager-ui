import QtQuick

// Provider side of `packages.show` and `packages.install` — the shell asking
// us to reveal a package, or to install it. Both are the same "find that row"
// problem and differ only in what they do once it lands, so they share a waiter.
QtObject {
    id: root

    property var store: null
    property var header: null
    property int timeoutMs: 8000

    function begin(requestId, name, intent) {
        // Every request gets an answer, including one displaced mid-wait.
        finish(false, "cancelled")
        d.requestId = requestId
        d.name = name || ""
        d.intent = intent
        deadline.restart()
        apply()
        attempt()
    }

    // Run the query on the user's behalf, exactly as if they had typed it.
    // One writer per piece of state: the header owns the query (and shows it),
    // the store owns the filters. Pushing the query from both ends meant two
    // round trips for every retry.
    function apply() {
        if (root.header) root.header.setSearchText(d.name)
        if (root.store) root.store.clearFilters()
    }

    function attempt() {
        if (d.requestId === "" || !root.store) return
        // A push made before the replica finished initialising went nowhere.
        // searchText echoing back from the source is what says one landed.
        if (root.store.searchText !== d.name) apply()

        var done = d.intent === "packages.install"
                 ? root.store.installPackageByName(d.name)
                 : root.store.showDetailsForName(d.name)
        // Answered on ARRIVAL. For install that means "the gate is up", never
        // "it is installed" — a real download runs far past the broker's
        // deadline, and both intents are handoff, so the user stays here to
        // watch it either way.
        if (done) finish(true, "")
    }

    function finish(ok, error) {
        var id = d.requestId
        if (id === "") return
        d.requestId = ""
        d.name = ""
        d.intent = ""
        deadline.stop()
        logos.respond(id, ok, ({}), error)
    }

    property QtObject d: QtObject {
        id: d

        // "" = nothing in flight, which is also what makes attempt() cheap
        // enough to hang off every model signal below.
        property string requestId: ""
        property string name: ""
        property string intent: ""
    }

    property Timer deadline: Timer {
        id: deadline
        interval: root.timeoutMs
        onTriggered: root.finish(false, "failed")
    }

    // Retry as the replica fills. dataChanged matters as much as rowsInserted:
    // prefetch delivers the rows first and their role values after, so the name
    // we match on can still be empty at insert time.
    property Connections modelSignals: Connections {
        target: root.store ? root.store.packagesModel : null
        ignoreUnknownSignals: true
        function onModelReset() { root.attempt() }
        function onRowsInserted() { root.attempt() }
        function onDataChanged() { root.attempt() }
    }

    // Backstop for the orderings where a property crosses the wire ahead of the
    // model's own signals.
    property Connections storeSignals: Connections {
        target: root.store
        ignoreUnknownSignals: true
        function onTotalCountChanged() { root.attempt() }
        function onSearchTextChanged() { root.attempt() }
    }
}
