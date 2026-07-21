import QtQuick
import QtQuick.Controls
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
                        // Bulk action surface (Run Actions button + the
                        // post-confirm popup) is intentionally hidden —
                        // per-row ActionPill is the single way to act.
                        // Backend still publishes runnableActionCount /
                        // actionSummary / actionPlanItems for any
                        // future re-introduction, but no QML reads them
                        // today. See the RunActionsConfirm comment block
                        // at the bottom for the rest of the rationale.
                        runnableActionCount: 0
                        actionSummary: ({})
                        stateIndex: store.installStateFilter
                        onReloadClicked: store.refreshCatalog()
                        onStateRequested: function(state) { store.setInstallStateFilter(state) }
                        onRepositoriesClicked: store.navigateToRepositories()
                    }

                    // Empty state — shown when the catalog has no packages and
                    // the backend is not loading (typically: no repos configured).
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: store.totalCount === 0 && !store.isLoading

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.spacing.medium
                            width: Math.min(parent.width * 0.6, 420)

                            LogosText {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("No repositories configured")
                                font.pixelSize: Theme.typography.subtitleText
                                font.weight: Theme.typography.weightMedium
                                color: Theme.palette.text
                                horizontalAlignment: Text.AlignHCenter
                            }

                            LogosText {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillWidth: true
                                text: qsTr("Add a package repository to browse and install plugins and modules.")
                                font.pixelSize: Theme.typography.primaryText
                                color: Theme.palette.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }

                            LogosButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: Theme.spacing.small
                                text: qsTr("Manage Repositories")
                                onClicked: store.navigateToRepositories()
                            }
                        }
                    }

                    PackageList {
                        id: packageList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: store.totalCount > 0 || store.isLoading

                        packagesModel: store.packagesModel
                        sortRole: store.sortRole
                        sortOrder: store.sortOrder
                        onDetailsRequested: function(i) { store.requestDetails(i) }
                        onSelectionToggled: function(i, checked) { store.toggleSelection(i, checked) }
                        // Per-row Uninstall (trash icon in the trailing cell).
                        // Reload is unsurfaced for now — the backend slot is a
                        // TODO stub; wire a UI affordance back in when the
                        // logoscore load/unload path lands.
                        onUninstallRequested: function(i) { store.uninstallPackage(i) }
                        // Per-row primary action (ActionPill click).
                        // Single (index, action) call instead of one
                        // signal per action type — store.runRowAction
                        // switches to the matching backend slot.
                        onActionRequested: function(i, action) { store.runRowAction(i, action) }
                        onVersionChanged: function(i, vi) { store.setRowVersion(i, vi) }
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

            DetailsPanel {
                Layout.preferredWidth: 320
                Layout.minimumWidth: 280
                Layout.maximumWidth: 380
                Layout.fillHeight: true
                visible: !!store.selectedPackageDetails && !!store.selectedPackageDetails.name
                details: store.selectedPackageDetails
                onCloseRequested: store.clearSelectedDetails()
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

    // ── Run Actions confirm popup ────────────── (currently unwired)
    //
    // The bulk-action surface (checkbox column + "Run Actions (N)"
    // header button + this confirm popup) is disabled. Per-row
    // ActionPill is the single way to act on a package right now.
    // Backend plumbing (actionSummary, actionPlanItems,
    // runSelectedActions) is kept so the bulk path can be turned
    // back on without a re-implementation, but no QML opens this
    // popup today.
    //
    // RunActionsConfirm {
    //     id: runActionsConfirm
    //     onConfirmed: store.runSelectedActions()
    // }

    // ── Per-row dep-confirm popup ─────────────────────────────────
    //
    // Fires when the backend's resolver preview surfaces transitive
    // changes for a per-row Install / Reinstall / Upgrade / Downgrade.
    // No-changes case proceeds silently (no popup). The three button
    // outcomes — with deps / just package / cancel — route through
    // BackendStore so the backend's PendingDepConfirm entry is drained
    // exactly once. See PackageManagerBackend::runDepPreviewForAction
    // and the .rep `installDepsConfirmationRequested` signal for the
    // wire format.
    InstallDepsConfirm {
        id: installDepsConfirm
        // The dialog echoes back the opaque requestKey (not the package
        // name) so the backend drains the exact pending entry — package
        // name isn't unique across repos.
        onConfirmedWithDeps:    function(key) { store.confirmInstallWithDeps(key) }
        onConfirmedWithoutDeps: function(key) { store.confirmInstallWithoutDeps(key) }
        onCancelled:            function(key) { store.cancelInstallConfirm(key) }
    }

    // Backend signal → dialog payload. Mirrors the .rep signature
    // (requestKey first), packed into the QVariantMap openWith() wants.
    Connections {
        target: store.backend
        ignoreUnknownSignals: true
        function onInstallDepsConfirmationRequested(requestKey, packageName,
                                                    displayName, actionLabel,
                                                    fromVersion, toVersion,
                                                    depChanges) {
            installDepsConfirm.openWith({
                requestKey:  requestKey,
                packageName: packageName,
                displayName: displayName,
                actionLabel: actionLabel,
                fromVersion: fromVersion,
                toVersion:   toVersion,
                depChanges:  depChanges
            })
        }
    }

}
