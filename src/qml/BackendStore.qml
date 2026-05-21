import QtQuick

import Logos.PackageManagerUi 1.0

// Backend adapter for the Package Manager UI.
QtObject {
    id: store

    objectName: "pmui.BackendStore"

    // ─── Properties: inputs (overridable for tests) ───
    property string moduleName: "package_manager_ui"
    property var backend: logos.module(moduleName)
    property var packagesModel: logos.model(moduleName, "packages")

    // ─── Properties: reactive state (bind from views) ───
    readonly property bool isInstalling: backend ? backend.isInstalling : false
    readonly property bool isLoading: backend ? backend.isLoading : false
    // Bulk "Run Actions" surface. Replaces the old has*Selection
    // booleans: the header reads the count for its label and the
    // confirm-summary popup reads the map for its per-action breakdown.
    readonly property int runnableActionCount: backend ? backend.runnableActionCount : 0
    readonly property var actionSummary: backend ? backend.actionSummary : ({})
    // Per-row breakdown matching actionSummary's category counts.
    // [{ name, displayName, action, repository, fromVersion, toVersion }, ...]
    // Consumed by the Run Actions confirm popup to render "name: vA → vB"
    // lines under each action header.
    readonly property var actionPlanItems: backend ? backend.actionPlanItems : []
    readonly property list<string> categories: backend ? backend.categories : []
    readonly property int selectedCategoryIndex: backend ? backend.selectedCategoryIndex : 0

    // Filter / sort / pagination state
    readonly property string searchText: backend ? backend.searchText : ""
    readonly property int installStateFilter: backend ? backend.installStateFilter : 0
    readonly property int pageSize: backend ? backend.pageSize : 20
    readonly property int currentPage: backend ? backend.currentPage : 1
    readonly property int totalCount: backend ? backend.totalCount : 0
    readonly property string sortRole: backend ? backend.sortRole : ""
    readonly property int sortOrder: backend ? backend.sortOrder : Qt.AscendingOrder

    readonly property list<string> availableTypes: backend ? backend.availableTypes : ["All"]
    readonly property int selectedTypeIndex: backend ? backend.selectedTypeIndex : 0

    // Multi-repo: list mirrors package_downloader.listRepositories().
    // Refreshed eagerly by the backend on construction + after every
    // add/remove/setEnabled call.
    readonly property var repositories: backend ? backend.repositories : []
    readonly property bool repositoriesLoading: backend ? backend.repositoriesLoading : false

    readonly property alias selectedPackageDetails: d.selectedPackageDetails

    property QtObject d: QtObject {
        id: d

        property var selectedPackageDetails: ({})

        property Connections conn: Connections {
            target: store.backend
            ignoreUnknownSignals: true

            function onPackageDetailsLoaded(details) {
                d.selectedPackageDetails = details || ({})
            }
        }
    }

    // ─── Methods: intents called by views ───
    function refreshCatalog() { if (backend) backend.refreshCatalog() }
    // New bulk path — used by the "Run Actions (N)" header button.
    // Backend builds the per-row action plan and dispatches installs
    // through the batched downloader + version changes per-row.
    function runSelectedActions() { if (backend) backend.runSelectedActions() }
    function selectCategory(i) { if (backend) backend.pushSelectedCategoryIndex(i) }
    function selectType(i) { if (backend) backend.pushSelectedTypeIndex(i) }
    function toggleSelection(i, checked) { if (backend) backend.togglePackage(i, checked) }
    function requestDetails(i) { if (backend) backend.requestPackageDetails(i) }
    function clearSelectedDetails() { d.selectedPackageDetails = ({}) }

    function installPackage(i) { if (backend) backend.installPackage(i) }
    function reloadPackage(i) { if (backend) backend.reloadPackage(i) }
    function upgradePackage(i) { if (backend) backend.upgradePackage(i) }
    function downgradePackage(i) { if (backend) backend.downgradePackage(i) }
    function reinstallPackage(i) { if (backend) backend.sidegradePackage(i) }
    function uninstallPackage(i) { if (backend) backend.uninstall(i) }

    // Dep-confirm dialog responses. Routed from
    // InstallDepsConfirm.qml's three signals back through the .rep
    // slots — keyed on the catalog packageName so a refresh between
    // click and confirm doesn't drop the dispatch. Backend's pending
    // entry is drained in the with-/without-deps slots, or by
    // cancelInstallConfirm.
    function confirmInstallWithDeps(name)    { if (backend) backend.confirmInstallWithDeps(name) }
    function confirmInstallWithoutDeps(name) { if (backend) backend.confirmInstallWithoutDeps(name) }
    function cancelInstallConfirm(name)      { if (backend) backend.cancelInstallConfirm(name) }

    // Dispatch the per-row primary action emitted by ActionPill. Keeps
    // the QML side from having to switch on the enum: it just forwards
    // (index, action) here and we route to the matching backend slot.
    // Mirrors PackageActionPlan's projection on the bulk side.
    function runRowAction(i, action) {
        if (!backend) return
        switch (action) {
        case PackageManagerUi.Install:   backend.installPackage(i);   break
        case PackageManagerUi.Retry:     backend.installPackage(i);   break
        case PackageManagerUi.Upgrade:   backend.upgradePackage(i);   break
        case PackageManagerUi.Downgrade: backend.downgradePackage(i); break
        case PackageManagerUi.Reinstall: backend.sidegradePackage(i); break
        // NoOp / NotAvailable — pill is non-clickable for these; this
        // is the safety net for stale events.
        default: break
        }
    }

    // Filter / sort / pagination intents — replica-side writers go
    // through QtRO's generated `push*` methods so the source-side
    // setter is invoked and the proxy resliсes the model.
    function setSearchText(text)         { if (backend) backend.pushSearchText(text) }
    function setInstallStateFilter(state){ if (backend) backend.pushInstallStateFilter(state) }
    function setPageSize(n)              { if (backend) backend.pushPageSize(n) }
    function setCurrentPage(p)           { if (backend) backend.pushCurrentPage(p) }
    function setSortRole(role)           { if (backend) backend.pushSortRole(role) }
    function setSortOrder(order)         { if (backend) backend.pushSortOrder(order) }

    // Multi-repo intents.
    function setRowVersion(i, vi)            { if (backend) backend.setRowVersion(i, vi) }
    function refreshRepositories()           { if (backend) backend.refreshRepositories() }
    function addRepository(url)              { if (backend) backend.addRepository(url) }
    function removeRepository(url)           { if (backend) backend.removeRepository(url) }
    function setRepositoryEnabled(url, on)   { if (backend) backend.setRepositoryEnabled(url, on) }
}
