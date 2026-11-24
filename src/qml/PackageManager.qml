import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.Layouts

import Panels

import "DownloadNotice.js" as DownloadNotice

Rectangle {

    id: root


    BackendStore {
        id: store
    }

    color: Theme.palette.background

    // A package's details are open, beside the list or, on a phone, the page.
    readonly property bool detailOpen: !!store.selectedPackageDetails
                                       && !!store.selectedPackageDetails.name

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin(root.width)
        spacing: Theme.spacing.xlarge

        // ─── Page header: title, subtitle and the search, a 🔍 on a phone ───
        LogosPageHeader {
            id: header
            objectName: "pmui.header"
            visible: !detailsPanel.showingPanel
            Layout.fillWidth: true
            title: qsTr("Package Manager")
            subtitle: qsTr("Manage your plugins and packages.")
            // On a phone, as Alisher's design has it: what the page lists,
            // and what its details go back to.
            shortTitle: qsTr("Packages")
            subtitleOnPhone: false
            searchEnabled: true
            searchPlaceholder: qsTr("Search packages…")
            searchShortcutHint: searchShortcut.nativeText
            searchWidth: 605
            onSearchTextChanged: {
                if (searchText !== store.searchText)
                    store.setSearchText(searchText)
            }
        }

        Shortcut {
            id: searchShortcut
            sequence: "Ctrl+K"
            context: Qt.WindowShortcut
            onActivated: header.openSearch()
        }

        // ─── Download notice ── why downloads may not work right now.
        LogosNotice {
            id: downloadNotice
            objectName: "pmui.downloadNotice"
            readonly property var notice: DownloadNotice.notice(
                store.online, store.downloadSource, store.storageState)
            Layout.fillWidth: true
            shown: notice !== null
            severity: !notice ? LogosNotice.Info
                    : notice.severity === "error" ? LogosNotice.Error
                    : notice.severity === "warning" ? LogosNotice.Warning
                    : LogosNotice.Info
            title: notice ? notice.title : ""
            message: notice ? notice.message : ""
        }

        // ─── Failure notice ── the only place a refused or unanswered
        // confirmation becomes visible; without it those outcomes look exactly
        // like nothing having happened.
        LogosNotice {
            id: messageNotice
            objectName: "pmui.messageNotice"
            Layout.fillWidth: true
            severity: LogosNotice.Error
            message: store.lastMessage
            closable: true
            shown: false
            onDismissed: store.dismissMessage()
        }

        Connections {
            target: store
            function onLastMessageChanged() {
                messageNotice.shown = store.lastMessage.length > 0
            }
        }

        // ─── Body: the categories and types beside the packages while the
        // list keeps its width; otherwise pickers in its toolbar — filters
        // are not navigation, as Alisher's RESPONSIVE.md §3.6 has it ───
        LogosSidebarLayout {
            id: body
            objectName: "pmui.body"
            Layout.fillWidth: true
            Layout.fillHeight: true

            side: CategorySidebar {
                categories: store.categories
                currentIndex: store.selectedCategoryIndex
                types: store.availableTypes
                currentTypeIndex: store.selectedTypeIndex
                onCategorySelected: function(i) { store.selectCategory(i) }
                onTypeSelected: function(i) { store.selectType(i) }
            }

            // A package's details, a card as before: beside the list, which
            // drops the columns they show, closed by the card's ✕; or on a
            // phone the page, under the side panel's "‹ Packages", the one way
            // back, as in Settings.
            LogosSidePanel {
                id: detailsPanel
                objectName: "pmui.detailsPanel"
                anchors.fill: parent
                spacing: Theme.spacing.medium
                divider: false
                // On a phone the bar names where back goes, as Alisher's
                // ADR-054 has it.
                title: showingPanel ? qsTr("Packages") : ""
                onPanelOpenChanged: if (!panelOpen) store.clearSelectedDetails()

                panel: DetailsPanel {
                    showHeader: !detailsPanel.showingPanel
                    details: store.selectedPackageDetails
                    row: store.selectedRow
                    // A table row carries its version, action and Uninstall;
                    // a card, or no list beside, leaves them to the details.
                    showActions: !detailsPanel.beside || packageList.showingCards
                    onCloseRequested: detailsPanel.panelOpen = false
                    onActionRequested: function(action) { store.runSelectedAction(action) }
                    onVersionPicked: function(versionIndex) { store.setSelectedVersion(versionIndex) }
                    onUninstallRequested: store.uninstallSelected()
                }

                // Right container — wraps the package table's own header (bulk
                // actions + release picker), the install-state tabs, the table
                // itself, and the paginator.
                content: Rectangle {
                    color: Theme.palette.surfaceRaised
                    radius: Theme.spacing.radiusXlarge
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: Theme.spacing.medium
                        anchors.bottomMargin: Theme.spacing.medium
                        spacing: Theme.spacing.medium

                        TableHeader {
                            id: tableHeader
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.spacing.medium
                            Layout.rightMargin: Theme.spacing.medium
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
                            showUnavailable: store.showUnavailable
                            onShowUnavailableRequested: function(show) { store.setShowUnavailable(show) }
                            showFilterPickers: !body.sideShown
                            categories: store.categories
                            categoryIndex: store.selectedCategoryIndex
                            types: store.availableTypes
                            typeIndex: store.selectedTypeIndex
                            onCategorySelected: function(i) { store.selectCategory(i) }
                            onTypeSelected: function(i) { store.selectType(i) }
                            onReloadClicked: store.refreshCatalog()
                            onInstallLocalClicked: installLocalDialog.open()
                            onStateRequested: function(state) { store.setInstallStateFilter(state) }
                            onRepositoriesClicked: store.navigateToRepositories()
                        }

                        // Empty state — only when no repositories are configured
                        // AND there are no local-only installed packages to fall
                        // back on. Any local rows keep the list rendered.
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: store.repositoryCount === 0
                                     && store.totalCount === 0
                                     && !store.isLoading

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

                        Item {
                            id: nothingForThisHost
                            objectName: "pmui.nothingForThisHostView"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: store.repositoryCount > 0
                                     && store.availableHereCount === 0
                                     && !store.showUnavailable
                                     && !store.isLoading

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: Theme.spacing.medium
                                width: Math.min(parent.width * 0.6, 460)

                                LogosText {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Nothing available for this platform")
                                    font.pixelSize: Theme.typography.subtitleText
                                    font.weight: Theme.typography.weightMedium
                                    color: Theme.palette.text
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                LogosText {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    text: (store.hostVariant.length > 0
                                           && !store.hostVariant.startsWith("unknown"))
                                          ? qsTr("No configured repository publishes a build for %1.")
                                                .arg(store.hostVariant)
                                          : qsTr("No configured repository publishes a build for this platform.")
                                    font.pixelSize: Theme.typography.primaryText
                                    color: Theme.palette.textSecondary
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }

                                // No call to action: the toolbar filter is the one
                                // way to ask for the hidden rows, and it stays on
                                // screen here — TableHeader is outside this view.
                            }
                        }

                        PackageList {
                            id: packageList
                            objectName: "pmui.packageList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: !nothingForThisHost.visible
                                     && (store.repositoryCount > 0
                                         || store.totalCount > 0
                                         || store.isLoading)

                            packagesModel: store.packagesModel
                            repositoryLabels: store.repositoryLabels
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
                            Layout.leftMargin: Theme.spacing.medium
                            Layout.rightMargin: Theme.spacing.medium
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
    }

    // A package picked in the list opens its details; closing them, by ✕, ‹,
    // Esc or Back, clears the pick.
    Connections {
        target: store
        function onSelectedPackageDetailsChanged() {
            if (root.detailOpen)
                detailsPanel.panelOpen = true
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
    // The plan's summary (actionSummary, actionPlanItems) is still
    // published, but runSelectedActions is a stub: bulk needs one host
    // confirmation for the whole batch before it can come back.
    //
    // RunActionsConfirm {
    //     id: runActionsConfirm
    //     onConfirmed: store.runSelectedActions()
    // }

    // ── Local .lgx picker ─────────────────────────────────────────
    FileDialog {
        id: installLocalDialog
        objectName: "pmui.installLocalDialog"
        title: qsTr("Select LGX Package to Install")
        modality: Qt.NonModal
        nameFilters: [qsTr("LGX Package (*.lgx)"), qsTr("All Files (*)")]
        onAccepted: store.installLocalPackage(selectedFile)
    }

    // Provider side of `packages.show` / `packages.install`, which intents_module
    // delivers to the backend. The waiter lives in Panels — this is just the
    // doorway.
    PackageRevealRequest {
        id: revealRequest
        store: store
        header: header
    }

    Connections {
        target: store.backend
        ignoreUnknownSignals: true
        function onRevealRequested(dispatch, intent, name) {
            revealRequest.begin(dispatch, name)
        }
    }
}
