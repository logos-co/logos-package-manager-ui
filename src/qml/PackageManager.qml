import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme

import Panels

Rectangle {
    
    // Backend adaptor
    BackendStore {
        id: store
    }

    color: Theme.palette.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xxlarge
        spacing: Theme.spacing.xlarge

        HeaderBar {
            Layout.fillWidth: true
            
            isInstalling: store.isInstalling
            isLoading: store.isLoading
            hasSelectedPackages: store.hasSelectedPackages
            releases: store.releases
            selectedReleaseIndex: store.selectedReleaseIndex
            onReloadClicked: store.reload()
            onInstallClicked: store.install()
            onReleaseSelected: function(i) { store.selectRelease(i) }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            orientation: Qt.Horizontal

            CategorySidebar {
                SplitView.preferredWidth: 100
                SplitView.minimumWidth: 150
                SplitView.maximumWidth: 250

                categories: store.categories
                currentIndex: store.selectedCategoryIndex
                enabled: !store.isInstalling
                onCategorySelected: function(i) { store.selectCategory(i) }
            }

            ColumnLayout {
                SplitView.fillWidth: true
                spacing: 0

                PackageList {
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.6

                    packagesModel: store.packagesModel
                    isLoading: store.isLoading
                    enabled: !store.isInstalling
                    onDetailsRequested: function(i) { store.requestDetails(i) }
                    onSelectionToggled: function(i, checked) { store.toggleSelection(i, checked) }
                    onUpgradeRequested: function(i) { store.upgradePackage(i) }
                    onDowngradeRequested: function(i) { store.downgradePackage(i) }
                    onReinstallRequested: function(i) { store.reinstallPackage(i) }
                    onUninstallRequested: function(i) { store.uninstallPackage(i) }
                }

                DetailsPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    text: store.detailsText
                }
            }
        }
    }
}
