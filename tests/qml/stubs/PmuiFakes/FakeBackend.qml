import QtQuick

// Stands in for the package_manager_ui backend in the view tests: sample
// packages that the filters, tabs, search and sort narrow, a row's details on
// request, and what the views asked for, counted. setRow changes a package as
// an install would.
QtObject {
    id: fake

    signal packageDetailsLoaded(var details)
    // intents_module's delivery of packages.show / packages.install, and the
    // answers to it.
    signal revealRequested(string dispatch, string intent, string name)
    property var revealAnswers: []
    function answerReveal(dispatch, ok, error) { revealAnswers = revealAnswers.concat([[dispatch, ok, error]]) }
    // What the view asked the backend to confirm with the host, in order.
    property var confirms: []
    function confirmInstall(name, version, repositoryUrl) { confirms = confirms.concat([["install", name]]) }
    function confirmUpgrade(name, version, mode, repositoryUrl) { confirms = confirms.concat([["upgrade", name, mode]]) }
    function confirmUninstall(name) { confirms = confirms.concat([["uninstall", name]]) }
    property int repositoryRequests: 0
    function manageRepositories() { repositoryRequests += 1 }
    signal errorOccurred(int code)

    property ListModel model: ListModel {}
    property var shown: []
    readonly property var all: [
     {
      "name": "waku_module",
      "moduleName": "waku_module",
      "displayName": "Waku",
      "description": "Peer-to-peer messaging transport.",
      "category": "Networking",
      "type": "Module",
      "version": "0.4.2",
      "installedVersion": "0.4.2",
      "installStatus": 2,
      "installType": "user",
      "rowAction": 6,
      "size": 5300000,
      "dateUpdated": "2026-10-09T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.4.2",
        "releasedAt": "2026-10-09",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "ab12cd34ef56",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "delivery_module",
      "moduleName": "delivery_module",
      "displayName": "Delivery",
      "description": "Reliable message delivery over Waku.",
      "category": "Networking",
      "type": "Module",
      "version": "0.3.0",
      "installedVersion": "",
      "installStatus": 0,
      "installType": "user",
      "rowAction": 0,
      "size": 9500000,
      "dateUpdated": "2026-10-05T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.3.0",
        "releasedAt": "2026-10-05",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "chat_module",
      "moduleName": "chat_module",
      "displayName": "Chat",
      "description": "Private group chat over Waku.",
      "category": "Chat",
      "type": "Module",
      "version": "0.4.2",
      "installedVersion": "",
      "installStatus": 0,
      "installType": "user",
      "rowAction": 0,
      "size": 5100000,
      "dateUpdated": "2026-10-09T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.4.2",
        "releasedAt": "2026-10-09",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "chat_ui",
      "moduleName": "chat_ui",
      "displayName": "Chat UI",
      "description": "The chat app.",
      "category": "Chat",
      "type": "UI app",
      "version": "0.4.0",
      "installedVersion": "0.3.1",
      "installStatus": 4,
      "installType": "user",
      "rowAction": 1,
      "size": 3800000,
      "dateUpdated": "2026-09-30T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 2,
       "0": {
        "version": "0.4.0",
        "releasedAt": "2026-09-30",
        "rootHash": "ab12cd34ef56"
       },
       "1": {
        "version": "0.3.1",
        "releasedAt": "2026-08-30",
        "rootHash": "77ee88aa99bb"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "ab12cd34ef56",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "storage_module",
      "moduleName": "storage_module",
      "displayName": "Storage",
      "description": "Decentralised file storage.",
      "category": "Storage",
      "type": "Module",
      "version": "0.3.0",
      "installedVersion": "0.3.0",
      "installStatus": 2,
      "installType": "user",
      "rowAction": 6,
      "size": 18000000,
      "dateUpdated": "2026-10-13T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.3.0",
        "releasedAt": "2026-10-13",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "ab12cd34ef56",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "storage_ui",
      "moduleName": "storage_ui",
      "displayName": "Storage UI",
      "description": "Browse and share files, a long description that a narrow table drops first.",
      "category": "Storage",
      "type": "UI app",
      "version": "0.3.0",
      "installedVersion": "",
      "installStatus": 0,
      "installType": "user",
      "rowAction": 0,
      "size": 4100000,
      "dateUpdated": "2026-10-11T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.3.0",
        "releasedAt": "2026-10-11",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "wallet_module",
      "moduleName": "wallet_module",
      "displayName": "Wallet",
      "description": "Keys and balances.",
      "category": "Wallets",
      "type": "Module",
      "version": "1.2.1",
      "installedVersion": "1.2.1",
      "installStatus": 2,
      "installType": "user",
      "rowAction": 6,
      "size": 9500000,
      "dateUpdated": "2026-10-05T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "1.2.1",
        "releasedAt": "2026-10-05",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "ab12cd34ef56",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "wallet_ui",
      "moduleName": "wallet_ui",
      "displayName": "Wallet UI",
      "description": "Send and receive tokens.",
      "category": "Wallets",
      "type": "UI app",
      "version": "1.2.1",
      "installedVersion": "",
      "installStatus": 0,
      "installType": "user",
      "rowAction": 0,
      "size": 47100000,
      "dateUpdated": "2026-10-06T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "1.2.1",
        "releasedAt": "2026-10-06",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "accounts_module",
      "moduleName": "accounts_module",
      "displayName": "Accounts",
      "description": "Profiles and identities.",
      "category": "Developer tools",
      "type": "Module",
      "version": "0.3.0",
      "installedVersion": "0.3.0",
      "installStatus": 2,
      "installType": "builtin",
      "rowAction": 6,
      "size": 2400000,
      "dateUpdated": "2026-10-13T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.3.0",
        "releasedAt": "2026-10-13",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "ab12cd34ef56",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 0,
      "available": true,
      "repositoryUrl": "https://example.org/logos-repo.json"
     },
     {
      "name": "lez_explorer_ui",
      "moduleName": "lez_explorer_ui",
      "displayName": "LEZ Explorer",
      "description": "Browse the execution zone.",
      "category": "Developer tools",
      "type": "UI app",
      "version": "0.1.4",
      "installedVersion": "",
      "installStatus": 0,
      "installType": "user",
      "rowAction": 5,
      "size": 6200000,
      "dateUpdated": "2026-10-02T10:00:00Z",
      "downloadSource": 2,
      "availableVersions": {
       "length": 1,
       "0": {
        "version": "0.1.4",
        "releasedAt": "2026-10-02",
        "rootHash": "ab12cd34ef56"
       }
      },
      "selectedVersionIndex": 0,
      "hash": "ab12cd34ef56",
      "installedHash": "",
      "sourceKey": "https://example.org/logos-repo.json",
      "downloadReceived": 0,
      "downloadTotal": 0,
      "notAvailableReason": 3,
      "available": false,
      "repositoryUrl": "https://example.org/logos-repo.json"
     }
    ]

    // A ListModel numbers its roles in the order of its first row's keys.
    readonly property var packageRoleIds: {
        const out = {}
        Object.keys(all[0]).forEach((key, role) => out[key] = role)
        return out
    }
    property bool isInstalling: false
    property bool isLoading: false
    property bool online: true
    property string downloadSource: "any"
    property string storageState: ""
    property int runnableActionCount: 0
    property var actionSummary: ({})
    property var actionPlanItems: []
    property var categories: ["All", "Networking", "Storage", "Wallets", "Chat", "Developer tools"]
    property int selectedCategoryIndex: 0
    property string searchText: ""
    property int installStateFilter: 0
    property bool showUnavailable: false
    property int pageSize: 20
    property int currentPage: 1
    property int totalCount: 0
    property int availableHereCount: 0
    property string hostVariant: "linux-arm64"
    property int repositoryCount: 1
    property var repositoryLabels: ({ "https://example.org/logos-repo.json": "Logos Modules" })
    property string sortRole: ""
    property int sortOrder: Qt.AscendingOrder
    property var availableTypes: ["All", "Module", "UI app"]
    property int selectedTypeIndex: 0

    property int reloads: 0
    property int localInstalls: 0
    property var versionPicks: []

    function refilter() {
        const category = selectedCategoryIndex > 0 ? categories[selectedCategoryIndex] : ""
        const type = selectedTypeIndex > 0 ? availableTypes[selectedTypeIndex] : ""
        const query = searchText.toLowerCase()
        let out = all.filter(r => (showUnavailable || r.available)
                             && (!category || r.category === category)
                             && (!type || r.type === type)
                             && (installStateFilter === 0
                                 || (installStateFilter === 1) === (r.installedVersion !== ""))
                             && (!query || r.displayName.toLowerCase().includes(query)))
        if (sortRole) {
            const sign = sortOrder === Qt.AscendingOrder ? 1 : -1
            out = out.slice().sort((a, b) => a[sortRole] < b[sortRole] ? -sign : a[sortRole] > b[sortRole] ? sign : 0)
        }
        shown = out
        model.clear()
        for (const r of out)
            model.append(r)
        totalCount = out.length
        availableHereCount = all.filter(r => r.available).length
    }

    function pushSelectedCategoryIndex(i) { selectedCategoryIndex = i; refilter() }
    function pushSelectedTypeIndex(i) { selectedTypeIndex = i; refilter() }
    function pushSearchText(text) { searchText = text; refilter() }
    function pushInstallStateFilter(state) { installStateFilter = state; refilter() }
    function pushShowUnavailable(show) { showUnavailable = show; refilter() }
    function pushPageSize(n) { pageSize = n }
    function pushCurrentPage(p) { currentPage = p }
    function pushSortRole(role) { sortRole = role; refilter() }
    function pushSortOrder(order) { sortOrder = order; refilter() }
    function requestPackageDetailsFor(name, repositoryUrl) {
        const row = shown.find(r => r.name === name && (r.repositoryUrl || "") === (repositoryUrl || ""))
        if (row)
            packageDetailsLoaded(Object.assign({}, row, { repositoryDisplayName: "Logos Modules" }))
    }
    function refreshCatalog() { reloads += 1; refilter() }
    function installLocalPackage(url) { localInstalls += 1 }
    function runSelectedActions() {}
    function togglePackage(i, checked) {}
    function reloadPackage(i) {}
    function setRowVersion(i, vi) {
        versionPicks = versionPicks.concat([[i, vi]])
        setRow(shown[i].name, { selectedVersionIndex: vi, version: shown[i].availableVersions[vi].version })
    }

    // Changes `name`'s package, and its row if listed.
    function setRow(name, fields) {
        Object.assign(all.find(r => r.name === name), fields)
        const i = shown.findIndex(r => r.name === name)
        for (const key in fields)
            if (i >= 0) model.setProperty(i, key, fields[key])
    }

    Component.onCompleted: refilter()
}
