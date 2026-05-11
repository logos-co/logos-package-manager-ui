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
    readonly property bool hasInstallableSelection: backend ? backend.hasInstallableSelection : false
    readonly property bool hasUninstallableSelection: backend ? backend.hasUninstallableSelection : false
    readonly property list<string> categories: backend ? backend.categories : []
    readonly property int selectedCategoryIndex: backend ? backend.selectedCategoryIndex : 0
    readonly property list<string> releases: backend ? backend.releases : []
    readonly property int selectedReleaseIndex: backend ? backend.selectedReleaseIndex : 0

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
    function installSelected() { if (backend) backend.installSelected() }
    function selectRelease(i) { if (backend) backend.pushSelectedReleaseIndex(i) }
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
    function uninstallSelected() { if (backend) backend.uninstallSelected() }

    // Filter / sort / pagination intents — replica-side writers go
    // through QtRO's generated `push*` methods so the source-side
    // setter is invoked and the proxy resliсes the model.
    function setSearchText(text)         { if (backend) backend.pushSearchText(text) }
    function setInstallStateFilter(state){ if (backend) backend.pushInstallStateFilter(state) }
    function setPageSize(n)              { if (backend) backend.pushPageSize(n) }
    function setCurrentPage(p)           { if (backend) backend.pushCurrentPage(p) }
    function setSortRole(role)           { if (backend) backend.pushSortRole(role) }
    function setSortOrder(order)         { if (backend) backend.pushSortOrder(order) }
}
