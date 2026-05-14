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
                        stateIndex: store.installStateFilter
                        onReloadClicked: store.refreshCatalog()
                        onInstallClicked: store.installSelected()
                        onUninstallClicked: store.uninstallSelected()
                        onStateRequested: function(state) { store.setInstallStateFilter(state) }
                        onRepositoriesClicked: repositoriesPopup.open()
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
                        onVersionChanged: function(i, vi) { store.setRowVersion(i, vi) }
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

    // ── Manage Repositories popup ─────────────────────────────────────────
    //
    // Surfaces the multi-repo API: lists configured repositories (default
    // first, then user repos), accepts a URL to add a new one, and offers
    // per-row enable / disable / remove. The hardcoded default repo
    // cannot be removed but can be disabled (the toggle writes
    // `defaultDisabled` to the persisted config).
    //
    // The list is sourced from `backend.repositories`, which the backend
    // refreshes after every mutation. Toast feedback flows through
    // `onRepositoryOperationCompleted` below.
    Popup {
        id: repositoriesPopup
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 720
        height: Math.min(parent.height - 80, 560)
        padding: Theme.spacing.medium

        background: Rectangle {
            color: Theme.palette.background
            border.color: Theme.palette.border
            border.width: 1
            radius: 6
        }

        property string lastError: ""

        Connections {
            target: store.backend
            ignoreUnknownSignals: true

            function onRepositoryOperationCompleted(operation, url, success, error) {
                // Stash the latest error on the popup so the panel can show
                // it inline. Successful ops clear the error.
                repositoriesPopup.lastError = success ? "" : error
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing.medium

            // Header
            RowLayout {
                Layout.fillWidth: true
                LogosText {
                    text: "Package Repositories"
                    font.pixelSize: Theme.typography.heading
                    font.weight: Theme.typography.weightBold
                    color: Theme.palette.text
                    Layout.fillWidth: true
                }
                LogosButton {
                    text: "Refresh"
                    onClicked: if (store.backend) store.backend.refreshRepositories()
                    implicitWidth: 90
                    implicitHeight: 28
                }
            }

            // Error / status banner
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 32 : 0
                visible: repositoriesPopup.lastError !== ""
                color: Theme.colors.getColor(Theme.palette.error, 0.15)
                border.color: Theme.palette.error
                border.width: 1
                radius: 4
                LogosText {
                    anchors.fill: parent
                    anchors.margins: 6
                    text: repositoriesPopup.lastError
                    color: Theme.palette.error
                    font.pixelSize: Theme.typography.secondaryText
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            // Repository list
            ScrollView {
                id: repoScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                // Without this the inner ColumnLayout's `width: parent.width`
                // is circular — parent is the Flickable contentItem which
                // sizes to its child. Pinning contentWidth to availableWidth
                // makes the list (and the URL LogosText inside) actually
                // span the popup width instead of collapsing to the longest
                // unbreakable token.
                contentWidth: availableWidth

                ColumnLayout {
                    width: repoScroll.availableWidth
                    spacing: 6

                    Repeater {
                        model: store.backend ? store.backend.repositories : []

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            // The inner ColumnLayout drives the row height;
                            // bind to its implicitHeight so the row always
                            // grows to fit its content (was previously
                            // anchors.fill which couldn't compute a sane
                            // height for wrapping LogosTexts, causing the
                            // URL and description to overlap).
                            implicitHeight: rowCol.implicitHeight + 16
                            color: Theme.palette.backgroundButton
                            border.color: Theme.palette.borderSubtle
                            border.width: 1
                            radius: 4

                            ColumnLayout {
                                id: rowCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    LogosText {
                                        text: (modelData.displayName && modelData.displayName !== "")
                                              ? modelData.displayName
                                              : (modelData.name && modelData.name !== ""
                                                 ? modelData.name
                                                 : "(unresolved)")
                                        font.weight: Theme.typography.weightBold
                                        font.pixelSize: Theme.typography.primaryText
                                        color: Theme.palette.text
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        visible: modelData.isDefault === true
                                        radius: 3
                                        color: Theme.colors.getColor(Theme.palette.info, 0.18)
                                        border.color: Theme.palette.info
                                        border.width: 1
                                        implicitWidth: defaultBadge.implicitWidth + 12
                                        implicitHeight: defaultBadge.implicitHeight + 4
                                        LogosText {
                                            id: defaultBadge
                                            anchors.centerIn: parent
                                            text: "default"
                                            color: Theme.palette.info
                                            font.pixelSize: Theme.typography.tertiaryText
                                        }
                                    }
                                    Rectangle {
                                        visible: modelData.enabled !== true
                                        radius: 3
                                        color: Theme.colors.getColor(Theme.palette.textTertiary, 0.18)
                                        border.color: Theme.palette.textTertiary
                                        border.width: 1
                                        implicitWidth: disabledBadge.implicitWidth + 12
                                        implicitHeight: disabledBadge.implicitHeight + 4
                                        LogosText {
                                            id: disabledBadge
                                            anchors.centerIn: parent
                                            text: "disabled"
                                            color: Theme.palette.textSecondary
                                            font.pixelSize: Theme.typography.tertiaryText
                                        }
                                    }
                                }

                                LogosText {
                                    text: modelData.url
                                    color: Theme.palette.textSecondary
                                    font.pixelSize: Theme.typography.tertiaryText
                                    Layout.fillWidth: true
                                    // Plain elide on a single line — wrap +
                                    // elide together don't sum a sane
                                    // implicitHeight, which made this
                                    // LogosText overlap with the description
                                    // below in the rendered popup.
                                    elide: Text.ElideMiddle
                                    maximumLineCount: 1
                                }

                                LogosText {
                                    visible: modelData.description && modelData.description !== ""
                                    text: modelData.description || ""
                                    color: Theme.palette.textSecondary
                                    font.pixelSize: Theme.typography.tertiaryText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                // resolveError shows a soft warning when the
                                // repo's logos-repo.json couldn't be fetched
                                // (offline, 404, bad URL...). We still render
                                // the row so the user can remove the bogus
                                // entry, but the catalog won't include it.
                                LogosText {
                                    visible: modelData.resolveError && modelData.resolveError !== ""
                                    text: "Error: " + (modelData.resolveError || "")
                                    color: Theme.palette.warningHover
                                    font.pixelSize: Theme.typography.tertiaryText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    LogosButton {
                                        text: modelData.enabled === true ? "Disable" : "Enable"
                                        implicitHeight: 26
                                        onClicked: if (store.backend) store.backend.setRepositoryEnabled(
                                            modelData.url, !(modelData.enabled === true))
                                    }
                                    Item { Layout.fillWidth: true }
                                    LogosButton {
                                        text: "Remove"
                                        implicitHeight: 26
                                        // Default repo cannot be removed
                                        // (registry rejects it server-side);
                                        // disable the button so the user
                                        // doesn't trigger an error toast.
                                        enabled: modelData.isDefault !== true
                                        onClicked: if (store.backend) store.backend.removeRepository(modelData.url)
                                    }
                                }
                            }
                        }
                    }

                    // Empty-state when no repos resolved yet.
                    LogosText {
                        visible: !store.backend
                                 || !store.backend.repositories
                                 || store.backend.repositories.length === 0
                        text: store.backend && store.backend.repositoriesLoading
                              ? "Loading…"
                              : "No repositories configured."
                        color: Theme.palette.textSecondary
                        Layout.alignment: Qt.AlignCenter
                        Layout.topMargin: 20
                    }
                }
            }

            // Add-repository row. Sizes to content (was previously pinned
            // to implicitHeight: 64 which compressed the plain TextField
            // into a non-interactive sliver). LogosTextField brings its
            // own implicitHeight: 40, so the row ends up ~80px tall.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: addRepoCol.implicitHeight + 16
                color: Theme.palette.backgroundButton
                border.color: Theme.palette.borderSubtle
                border.width: 1
                radius: 4

                ColumnLayout {
                    id: addRepoCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 4

                    LogosText {
                        text: "Add a repository (URL to logos-repo.json)"
                        color: Theme.palette.textSecondary
                        font.pixelSize: Theme.typography.tertiaryText
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        LogosTextField {
                            id: addUrlField
                            Layout.fillWidth: true
                            placeholderText: "https://raw.githubusercontent.com/<owner>/<repo>/HEAD/logos-repo.json"
                        }
                        LogosButton {
                            text: "Add"
                            implicitHeight: 28
                            enabled: addUrlField.text.length > 0
                            onClicked: {
                                if (store.backend) store.backend.addRepository(addUrlField.text)
                                addUrlField.text = ""
                            }
                        }
                    }
                }
            }

            // Footer
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LogosButton {
                    text: "Close"
                    implicitWidth: 90
                    implicitHeight: 28
                    onClicked: repositoriesPopup.close()
                }
            }
        }
    }
}
