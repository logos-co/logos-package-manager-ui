import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

import Panels

Rectangle {

    id: root


    BackendStore {
        id: store 
        function onSelectionsCleared() { packageList.clearSelections() }
    }

    color: Theme.palette.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xxlarge
        spacing: Theme.spacing.xlarge

        // ─── Page header: Title + Subtitle + Search ───
        HeaderBar {
            Layout.fillWidth: true

            searchText: store.searchText
            onSearchEdited: function(text) { store.setSearchText(text) }
        }

        // ─── Body: Categories sidebar (left) | Right container (right) ───
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.medium

            CategorySidebar {
                Layout.preferredWidth: 200
                Layout.minimumWidth: 160
                Layout.maximumWidth: 200
                Layout.fillHeight: true

                categories: store.categories
                currentIndex: store.selectedCategoryIndex
                types: store.availableTypes
                currentTypeIndex: store.selectedTypeIndex
                onCategorySelected: function(i) { store.selectCategory(i) }
                onTypeSelected: function(i) { store.selectType(i) }
            }

            // Right container — wraps the package table's own header (bulk
            // actions + release picker), the install-state tabs, the table
            // itself, and the paginator.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.palette.surfaceRaised
                radius: Theme.spacing.radiusXlarge
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.medium
                    spacing: Theme.spacing.medium

                    TableHeader {
                        id: tableHeader
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0

                        isInstalling: store.isInstalling
                        isLoading: store.isLoading
                        hasInstallableSelection: store.hasInstallableSelection
                        hasUninstallableSelection: store.hasUninstallableSelection
                        releases: store.releases
                        selectedReleaseIndex: store.selectedReleaseIndex
                        stateIndex: store.installStateFilter
                        onReloadClicked: store.refreshCatalog()
                        onInstallClicked: store.installSelected()
                        onUninstallClicked: store.uninstallSelected()
                        onReleaseSelected: function(i) { store.selectRelease(i) }
                        onStateRequested: function(state) { store.setInstallStateFilter(state) }
                    }

                    PackageList {
                        id: packageList
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        packagesModel: store.packagesModel
                        sortRole: store.sortRole
                        sortOrder: store.sortOrder
                        onDetailsRequested: function(i) { store.requestDetails(i) }
                        onSelectionToggled: function(i, checked) { store.toggleSelection(i, checked) }
                        onReloadRequested: function(i) { store.reloadPackage(i) }
                        onInstallRequested: function(i) { store.installPackage(i) }
                        onUninstallRequested: function(i) { store.uninstallPackage(i) }
                        // TODO: open a per-row action menu (Show details,
                        // Reinstall, Copy module name, …). No backend
                        // wiring yet.
                        onMoreRequested: function(i) { /* TODO */ }
                        onSortRequested: function(role, order) {
                            store.setSortRole(role)
                            store.setSortOrder(order)
                        }
                    }

                    LogosPaginator {
                        id: paginator
                        Layout.fillWidth: true
                        visible: store.totalCount > 0
                        pageInfoText: qsTr("Select a package to view details.")
                        totalCount: store.totalCount
                        pageSize: store.pageSize
                        currentPage: store.currentPage
                        pageSizeOptions: [10, 20, 50, 100]

                        onPageRequested:     function(page) { store.setCurrentPage(page) }
                        onPageSizeRequested: function(size) { store.setPageSize(size) }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colors.getColor(Theme.palette.background, 0.65)
        visible: store.isLoading
        z: 1

        MouseArea { anchors.fill: parent }

        LogosSpinner {
            anchors.centerIn: parent
            implicitWidth: 36
            implicitHeight: 36
            running: parent.visible
        }
    }
}
