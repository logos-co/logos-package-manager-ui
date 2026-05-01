import QtQuick

import Logos.PackageManagerUi 1.0

// Backend adapter for the Package Manager UI.
QtObject {
    id: store

    // ─── Properties: inputs (overridable for tests) ───
    property string moduleName: "package_manager_ui"
    property var backend: logos.module(moduleName)
    property var packagesModel: logos.model(moduleName, "packages")

    // ─── Properties: reactive state (bind from views) ───
    readonly property bool isInstalling: backend ? backend.isInstalling : false
    readonly property bool isLoading: backend ? backend.isLoading : false
    readonly property bool hasSelectedPackages: backend ? backend.hasSelectedPackages : false
    readonly property list<string> categories: backend ? backend.categories : []
    readonly property int selectedCategoryIndex: backend ? backend.selectedCategoryIndex : 0
    readonly property list<string> releases: backend ? backend.releases : []
    readonly property int selectedReleaseIndex: backend ? backend.selectedReleaseIndex : 0

    // Derived from backend signal events (handled inside `d`), not a direct property.
    property string detailsText: qsTr("Select a package to view its details.")

    property QtObject d: QtObject {
        id: d

        property Connections conn: Connections {
            target: store.backend
            ignoreUnknownSignals: true

            function onErrorOccurred(errorType) {
                store.detailsText = d._errorText(errorType)
            }

            function onCancellationOccurred(name, message) {
                store.detailsText = message
            }

            function onInstallationProgressUpdated(progressType, packageName, completed, total, success, error) {
                store.detailsText = d._progressText(progressType, packageName, completed, total, success, error)
            }

            function onPackageDetailsLoaded(details) {
                store.detailsText = d._formatDetails(details)
            }
        }

        function _errorText(t) {
            switch (t) {
            case PackageManagerUi.InstallationAlreadyInProgress:
                return qsTr("Error: Installation already in progress.\nPlease wait for it to complete.")
            case PackageManagerUi.NoPackagesSelected:
                return qsTr("Error: No packages selected.\nSelect at least one package to install.")
            case PackageManagerUi.PackageManagerNotConnected:
                return qsTr("Error: Package manager not connected")
            case PackageManagerUi.UninstallFailed:
                return qsTr("Error: Uninstall failed.")
            case PackageManagerUi.PackageNotUninstallable:
                return qsTr("Error: This package is embedded in the build and cannot be uninstalled.")
            }
            return ""
        }

        function _progressText(phase, pkg, completed, total, success, error) {
            switch (phase) {
            case PackageManagerUi.Started:
                return qsTr("Starting Installation...\n%1 package(s) queued.").arg(total)
            case PackageManagerUi.InProgress:
                if (success) return qsTr("Successfully installed: %1\nProgress: %2/%3 packages")
                                    .arg(pkg).arg(completed).arg(total)
                return ""
            case PackageManagerUi.ProgressFailed:
                return qsTr("Failed to install: %1\nError: %2\nProgress: %3/%4 packages")
                       .arg(pkg).arg(error).arg(completed).arg(total)
            case PackageManagerUi.Completed:
                return qsTr("Installation Complete\nFinished installing %1 package(s).").arg(completed)
            }
            return ""
        }

        function _formatDetails(detail) {
            var out = detail.name + "\n\n"

            if (detail.moduleName && detail.moduleName !== detail.name) {
                out += qsTr("Module Name: %1").arg(detail.moduleName) + "\n"
            }
            out += qsTr("Description: %1").arg(detail.description || qsTr("No description available")) + "\n\n"
            if (detail.type) out += qsTr("Type: %1").arg(detail.type) + "\n"
            if (detail.category) out += qsTr("Category: %1").arg(detail.category) + "\n"

            var status = detail.installStatus | 0
            var releaseVersion = detail.version || ""
            var releaseHash = detail.hash || ""
            var installedVersion = detail.installedVersion || ""
            var installedHash = detail.installedHash || ""

            if (status === PackageManagerUi.Installed) {
                out += qsTr("Status: Installed") + "\n"
                if (installedVersion) out += qsTr("Installed version: %1").arg(installedVersion) + "\n"
                if (installedHash) out += qsTr("Installed hash: %1").arg(d._shortHash(installedHash)) + "\n"
            } else if (status === PackageManagerUi.Installing) {
                out += qsTr("Status: Installing…") + "\n"
            } else if (status === PackageManagerUi.Failed) {
                out += qsTr("Status: Failed") + "\n"
                if (detail.errorMessage) out += qsTr("Error: %1").arg(detail.errorMessage) + "\n"
            } else if (status === PackageManagerUi.UpgradeAvailable
                       || status === PackageManagerUi.DowngradeAvailable
                       || status === PackageManagerUi.DifferentHash) {
                if (status === PackageManagerUi.UpgradeAvailable) out += qsTr("Status: Upgrade Available") + "\n"
                else if (status === PackageManagerUi.DowngradeAvailable) out += qsTr("Status: Downgrade Available") + "\n"
                else out += qsTr("Status: Different Hash") + "\n"
                out += qsTr("Installed: %1 (%2)").arg(installedVersion).arg(d._shortHash(installedHash)) + "\n"
                out += qsTr("Release:   %1 (%2)").arg(releaseVersion).arg(d._shortHash(releaseHash)) + "\n"
            } else {
                out += qsTr("Status: Not Installed") + "\n"
                if (releaseVersion) out += qsTr("Release version: %1").arg(releaseVersion) + "\n"
                if (releaseHash) out += qsTr("Release hash: %1").arg(d._shortHash(releaseHash)) + "\n"
            }

            var deps = detail.dependencies
            if (deps && deps.length > 0) {
                out += "\n" + qsTr("Dependencies:") + "\n"
                for (var i = 0; i < deps.length; i++) out += "  • " + deps[i] + "\n"
            } else {
                out += "\n" + qsTr("Dependencies: None") + "\n"
            }
            return out
        }

        function _shortHash(h) {
            if (!h) return ""
            if (h.length <= 16) return h
            return h.substring(0, 8) + "…" + h.substring(h.length - 8)
        }
    }

    // ─── Methods: intents called by views ───
    function reload() { if (backend) backend.reload() }
    function install() { if (backend) backend.install() }
    function selectRelease(i) { if (backend) backend.pushSelectedReleaseIndex(i) }
    function selectCategory(i) { if (backend) backend.pushSelectedCategoryIndex(i) }
    function toggleSelection(i, checked) { if (backend) backend.togglePackage(i, checked) }
    function requestDetails(i) { if (backend) backend.requestPackageDetails(i) }

    function upgradePackage(i) { if (backend) backend.upgradePackage(i) }
    function downgradePackage(i) { if (backend) backend.downgradePackage(i) }
    function reinstallPackage(i) { if (backend) backend.sidegradePackage(i) }
    function uninstallPackage(i) { if (backend) backend.uninstall(i) }
}
