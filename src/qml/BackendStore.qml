import QtQuick

import Logos.PackageManagerUi 1.0

// Backend adapter for the Package Manager UI.
QtObject {
    id: store

    objectName: "pmui.BackendStore"

    // ─── Properties: inputs (overridable for tests) ───
    property string moduleName: "package_manager_ui"
    property var backend: logos.module(moduleName)
    // prefetch=true: the replica caches all roles for the page before it
    // reports populated, so rows never render empty
    property var packagesModel: logos.model(moduleName, "packages", true)
    readonly property var packageRoleIds: {
        var out = ({})
        var src = backend ? backend.packageRoleIds : null
        if (src) for (var k in src) out[k] = src[k]
        return out
    }

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
    readonly property int repositoryCount: backend ? backend.repositoryCount : 0
    readonly property string sortRole: backend ? backend.sortRole : ""
    readonly property int sortOrder: backend ? backend.sortOrder : Qt.AscendingOrder

    readonly property list<string> availableTypes: backend ? backend.availableTypes : ["All"]
    readonly property int selectedTypeIndex: backend ? backend.selectedTypeIndex : 0

    readonly property alias selectedPackageDetails: d.selectedPackageDetails

    // Last user-facing failure, "" when there is nothing to show. Every backend
    // failure channel lands here — without this the confirm flow fails silently,
    // since a refused or unanswered intent has no other visible outcome.
    readonly property alias lastMessage: d.lastMessage
    function dismissMessage() { d.lastMessage = "" }

    property QtObject d: QtObject {
        id: d

        property var selectedPackageDetails: ({})
        property int selectedPackageIndex: -1
        property string lastMessage: ""

        function canRequest() {
            return typeof logos !== "undefined" && typeof logos.request === "function"
        }

        // Ask the host, then run `onApproved` if it said yes. Every other
        // outcome — including the user's own cancel — goes to
        // reportActionFailed, which owns both the cleanup and the decision to
        // stay quiet.
        function confirm(intent, names, params, onApproved) {
            if (!canRequest()) {
                console.warn("PMUI: no logos.request on this host — cannot confirm", intent)
                if (store.backend) store.backend.reportActionFailed(names, "unavailable")
                return
            }
            logos.request(intent, params, function (result) {
                if (!store.backend) return
                if (result && result.ok) { onApproved(); return }
                store.backend.reportActionFailed(
                    names, result ? (result.error || "failed") : "failed")
            })
        }

        // Read the fields the confirm payloads need out of the paging proxy.
        // Returns null when the row is gone — a refresh can land between render
        // and click.
        function row(i) {
            if (!store.packagesModel || !store.packageRoleIds) return null
            var idx = store.packagesModel.index(i, 0)
            if (!idx.valid) return null
            var roles = store.packageRoleIds
            return {
                name:          store.packagesModel.data(idx, roles.name) || "",
                moduleName:    store.packagesModel.data(idx, roles.moduleName) || "",
                version:       store.packagesModel.data(idx, roles.version) || "",
                repositoryUrl: store.packagesModel.data(idx, roles.repositoryUrl) || ""
            }
        }

        function confirmInstall(r) {
            if (!r.name) return
            confirm("logos.packages.confirm_install", [r.name],
                { name: r.name, version: r.version, repositoryUrl: r.repositoryUrl },
                function () {
                    store.backend.performInstall(r.name, r.version, r.repositoryUrl)
                })
        }

        function confirmUpgrade(r, mode) {
            if (!r.moduleName) return
            confirm("logos.packages.confirm_upgrade", [r.moduleName],
                { name: r.moduleName, version: r.version, mode: mode,
                  repositoryUrl: r.repositoryUrl },
                function () {
                    store.backend.performUpgrade(r.moduleName, r.version, mode,
                                                 r.repositoryUrl)
                })
        }

        function confirmUninstall(r) {
            if (!r.moduleName) return
            confirm("logos.packages.confirm_uninstall", [r.moduleName],
                { names: [r.moduleName] },
                function () { store.backend.performUninstall([r.moduleName]) })
        }

        function errorText(code) {
            switch (code) {
            case PackageManagerUi.InstallationAlreadyInProgress:
                return qsTr("An installation is already in progress.")
            case PackageManagerUi.NoPackagesSelected:
                return qsTr("No packages selected.")
            case PackageManagerUi.PackageManagerNotConnected:
                return qsTr("The package manager is not connected.")
            case PackageManagerUi.UninstallFailed:
                return qsTr("Uninstall failed.")
            case PackageManagerUi.PackageNotUninstallable:
                return qsTr("That package cannot be uninstalled.")
            case PackageManagerUi.LocalPackageInvalid:
                return qsTr("That file is not a valid .lgx package.")
            default:
                return ""
            }
        }

        property Connections conn: Connections {
            target: store.backend
            ignoreUnknownSignals: true

            function onPackageDetailsLoaded(details) {
                d.selectedPackageDetails = details || ({})
            }

            function onCancellationOccurred(name, message) { d.lastMessage = message }
            function onErrorOccurred(code) {
                var text = d.errorText(code)
                if (text.length > 0) d.lastMessage = text
            }

            // A local .lgx, once the backend has read its manifest — the one
            // action whose identity QML cannot determine for itself. No repo
            // url: the file is already on disk.
            function onLocalPackageInspected(name, version, mode, isUpgrade) {
                // Reuse the row-shaped payload the confirm helpers expect; a
                // local file has no repository to pin against.
                var r = { name: name, moduleName: name, version: version,
                          repositoryUrl: "" }
                if (isUpgrade) d.confirmUpgrade(r, mode)
                else           d.confirmInstall(r)
            }
        }
    }

    // ─── Methods: intents called by views ───
    function refreshCatalog() { if (backend) backend.refreshCatalog() }
    function installLocalPackage(url) { if (backend) backend.installLocalPackage(url) }
    // Unwired — the bulk surface is off, and the backend slot is a stub.
    function runSelectedActions() { if (backend) backend.runSelectedActions() }
    function selectCategory(i) { if (backend) backend.pushSelectedCategoryIndex(i) }
    function selectType(i) { if (backend) backend.pushSelectedTypeIndex(i) }
    function toggleSelection(i, checked) { if (backend) backend.togglePackage(i, checked) }
    function requestDetails(i) {
        if (!backend) return
        d.selectedPackageIndex = i
        backend.requestPackageDetails(i)
    }
    // Find a package's row by name and open its details panel..
    function showDetailsForName(name) {
        if (!packagesModel || !name) return false
        var nameRole = packageRoleIds ? packageRoleIds.name : undefined
        if (nameRole === undefined) return false

        for (var i = 0; i < packagesModel.rowCount(); ++i) {
            var idx = packagesModel.index(i, 0)
            if (packagesModel.data(idx, nameRole) === name) {
                requestDetails(i)
                return true
            }
        }
        return false
    }

    function clearSelectedDetails() {
        d.selectedPackageDetails = ({})
        d.selectedPackageIndex = -1
    }

    function reloadPackage(i) { if (backend) backend.reloadPackage(i) }

    // ─── Per-row actions ───
    //
    // Each raises its confirm intent DIRECTLY from the click. Nothing goes to
    // the backend first: every field the payload needs is already in the model,
    // and the host resolves the dependency changes itself rather than trusting
    // ours. The backend is only called once the host has approved.
    function installPackage(i)   { var r = d.row(i); if (r) d.confirmInstall(r) }
    function upgradePackage(i)   { var r = d.row(i); if (r) d.confirmUpgrade(r, 0) }
    function downgradePackage(i) { var r = d.row(i); if (r) d.confirmUpgrade(r, 1) }
    function reinstallPackage(i) { var r = d.row(i); if (r) d.confirmUpgrade(r, 2) }
    function uninstallPackage(i) { var r = d.row(i); if (r) d.confirmUninstall(r) }

    // Dispatch the per-row primary action emitted by ActionPill. Keeps
    // the QML side from having to switch on the enum: it just forwards
    // (index, action) here and we route to the matching backend slot.
    // Mirrors PackageActionPlan's projection on the bulk side.
    function runRowAction(i, action) {
        if (!backend) return
        switch (action) {
        case PackageManagerUi.Install:   installPackage(i);   break
        case PackageManagerUi.Retry:     installPackage(i);   break
        case PackageManagerUi.Upgrade:   upgradePackage(i);   break
        case PackageManagerUi.Downgrade: downgradePackage(i); break
        case PackageManagerUi.Reinstall: reinstallPackage(i); break
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

    // Per-row version change. Also refetches details when the change is
    // on the row currently shown in the details panel
    function setRowVersion(i, vi) {
        if (!backend) return
        backend.setRowVersion(i, vi)
        if (i === d.selectedPackageIndex && d.selectedPackageDetails && d.selectedPackageDetails.name) {
            backend.requestPackageDetails(i)
        }
    }

    // Ask the shell to show Settings → Repositories.
    function navigateToRepositories() {
        if (typeof logos === "undefined" || typeof logos.request !== "function") {
            console.warn("PMUI: no logos.request on this host — cannot open Repositories")
            return
        }
        logos.request("logos.repositories.manage", {}, function (result) {
            if (!result || !result.ok)
                console.warn("PMUI: repositories intent failed:",
                             result ? result.error : "no result")
        })
    }
}
