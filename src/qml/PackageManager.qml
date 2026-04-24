import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

/* Enums (InstallStatus, ErrorType, ProgressType) from package_manager_ui.rep,
registered with QML by the replica factory plugin. */
import Logos.PackageManagerUi 1.0

// TODO: this file should be refactored to be smaller and split into reusable components, use Controls etc..
Rectangle {
    id: root

    QtObject {
        id: d
            readonly property var backend: logos.module(mod)
            readonly property var packagesModel: logos.model(mod, "packages")
            readonly property string mod: "package_manager_ui"
            property string detailsText: "Select a package to view its details."

            function shortHash(h) {
                if (!h) return ""
                if (h.length <= 16) return h
                return h.substring(0, 8) + "…" + h.substring(h.length - 8)
            }

            function statusBaseColor(row) {
                if (row.isVariantAvailable !== true) return Theme.palette.warning
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return Theme.palette.warning
                if (s === PackageManagerUi.Installed) return Theme.palette.success
                if (s === PackageManagerUi.Failed) return Theme.palette.error
                if (s === PackageManagerUi.UpgradeAvailable) return Theme.palette.info
                if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.accentBurntOrange
                if (s === PackageManagerUi.DifferentHash) return Theme.palette.accentOrangeMid
                return Theme.palette.textTertiary
            }

            function statusTextColor(row) {
                if (row.isVariantAvailable !== true) return Theme.palette.warningHover
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return Theme.palette.warningHover
                if (s === PackageManagerUi.Installed) return Theme.palette.successHover
                if (s === PackageManagerUi.Failed) return Theme.palette.errorHover
                if (s === PackageManagerUi.UpgradeAvailable) return Theme.palette.info
                if (s === PackageManagerUi.DowngradeAvailable) return Theme.palette.accentOrangeMid
                if (s === PackageManagerUi.DifferentHash) return Theme.palette.accentOrange
                return Theme.palette.textSecondary
            }

            function statusBgColor(row) {
                return Theme.colors.getColor(d.statusBaseColor(row), 0.18)
            }

            function statusText(row) {
                if (row.isVariantAvailable !== true) return "Not Available"
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return "Installing…"
                if (s === PackageManagerUi.Installed) return "Installed"
                if (s === PackageManagerUi.Failed) return "Failed"
                if (s === PackageManagerUi.UpgradeAvailable) return "Upgrade"
                if (s === PackageManagerUi.DowngradeAvailable) return "Downgrade"
                if (s === PackageManagerUi.DifferentHash) return "Different Hash"
                return "Not Installed"
            }

            // Label for the per-row action button. Empty string hides it.
            // The catalog surfaces three "swap" affordances based on version
            // comparison (Upgrade/Downgrade/Reinstall) plus Uninstall for any
            // user-installed package; embedded packages and not-installed rows
            // show no action button (the batch-install checkbox handles those).
            function rowActionLabel(row) {
                var s = row.installStatus | 0
                if (s === PackageManagerUi.UpgradeAvailable) return "Upgrade"
                if (s === PackageManagerUi.DowngradeAvailable) return "Downgrade"
                if (s === PackageManagerUi.DifferentHash) return "Reinstall"
                if (s === PackageManagerUi.Installed
                    && row.installType === "user") return "Uninstall"
                return ""
            }

            function runRowAction(row, index) {
                if (!d.backend) return
                var s = row.installStatus | 0
                // All four actions are stateless one-shot calls into the
                // backend, keyed by `index` into the model. The backend
                // resolves moduleName locally via PackageListModel::packageAt
                // — reading moduleName through the replica side (`row.moduleName`)
                // can return empty briefly right after a model reset because
                // QRO role data is re-synced asynchronously.
                //
                // The package_manager module owns the confirmation protocol
                // and emits beforeUninstall / beforeUpgrade events; Basecamp
                // catches those and drives the cascade dialog. PMU holds no
                // dialog state of its own.
                if (s === PackageManagerUi.UpgradeAvailable)   d.backend.upgradePackage(index)
                else if (s === PackageManagerUi.DowngradeAvailable) d.backend.downgradePackage(index)
                else if (s === PackageManagerUi.DifferentHash) d.backend.sidegradePackage(index)
                else if (s === PackageManagerUi.Installed
                         && row.installType === "user") {
                    d.backend.uninstall(index)
                }
            }
    }

    color: Theme.palette.background

    Connections {
        target: d.backend
        ignoreUnknownSignals: true

        function onErrorOccurred(errorType) {
            switch(errorType) {
                case PackageManagerUi.InstallationAlreadyInProgress:
                    d.detailsText = "Error: Installation already in progress.\nPlease wait for it to complete."
                    break
                case PackageManagerUi.NoPackagesSelected:
                    d.detailsText = "Error: No packages selected.\nSelect at least one package to install."
                    break
                case PackageManagerUi.PackageManagerNotConnected:
                    d.detailsText = "Error: Package manager not connected"
                    break
                case PackageManagerUi.UninstallFailed:
                    d.detailsText = "Error: Uninstall failed."
                    break
                case PackageManagerUi.PackageNotUninstallable:
                    d.detailsText = "Error: This package is embedded in the build and cannot be uninstalled."
                    break
            }
        }

        function onCancellationOccurred(name, message) {
            // Backend pre-formats `message` (e.g. "Uninstall of 'foo'
            // cancelled: timeout") so QML renders it as-is, avoiding the
            // "Failed to install" prefix that the progress channel would
            // otherwise impose on uninstall/upgrade cancellations.
            d.detailsText = message
        }

        function onInstallationProgressUpdated(progressType, packageName, completed, total, success, error) {
            switch(progressType) {
                case PackageManagerUi.Started:
                    d.detailsText = "Starting Installation...\n" + total + " package(s) queued."
                    break
                case PackageManagerUi.InProgress:
                    if (success) {
                        d.detailsText = "Successfully installed: " + packageName + "\nProgress: " + completed + "/" + total + " packages"
                    }
                    break
                case PackageManagerUi.ProgressFailed:
                    d.detailsText = "Failed to install: " + packageName + "\nError: " + error + "\nProgress: " + completed + "/" + total + " packages"
                    break
                case PackageManagerUi.Completed:
                    d.detailsText = "Installation Complete\nFinished installing " + completed + " package(s)."
                    break
            }
        }
        
function onPackageDetailsLoaded(details) {
            // Format package details as plain text
            var text = details.name + "\n\n"

            if (details.moduleName && details.moduleName !== details.name) {
                text += "Module Name: " + details.moduleName + "\n"
            }

            text += "Description: " + (details.description || "No description available") + "\n\n"

            if (details.type) {
                text += "Type: " + details.type + "\n"
            }

            if (details.category) {
                text += "Category: " + details.category + "\n"
            }

            // Status block
            var status = details.installStatus | 0
            var releaseVersion = details.version || ""
            var releaseHash = details.hash || ""
            var installedVersion = details.installedVersion || ""
            var installedHash = details.installedHash || ""

            if (status === PackageManagerUi.Installed) {
                text += "Status: Installed\n"
                if (installedVersion) text += "Installed version: " + installedVersion + "\n"
                if (installedHash) text += "Installed hash: " + d.shortHash(installedHash) + "\n"
            } else if (status === PackageManagerUi.Installing) {
                text += "Status: Installing…\n"
            } else if (status === PackageManagerUi.Failed) {
                text += "Status: Failed\n"
                if (details.errorMessage) {
                    text += "Error: " + details.errorMessage + "\n"
                }
            } else if (status === PackageManagerUi.UpgradeAvailable
                       || status === PackageManagerUi.DowngradeAvailable
                       || status === PackageManagerUi.DifferentHash) {
                if (status === PackageManagerUi.UpgradeAvailable) text += "Status: Upgrade Available\n"
                else if (status === PackageManagerUi.DowngradeAvailable) text += "Status: Downgrade Available\n"
                else text += "Status: Different Hash\n"
                text += "Installed: " + installedVersion + " (" + d.shortHash(installedHash) + ")\n"
                text += "Release:   " + releaseVersion + " (" + d.shortHash(releaseHash) + ")\n"
            } else {
                text += "Status: Not Installed\n"
                if (releaseVersion) text += "Release version: " + releaseVersion + "\n"
                if (releaseHash) text += "Release hash: " + d.shortHash(releaseHash) + "\n"
            }

            // Dependencies
            var deps = details.dependencies
            if (deps && deps.length > 0) {
                text += "\nDependencies:\n"
                for (var i = 0; i < deps.length; i++) {
                    text += "  • " + deps[i] + "\n"
                }
            } else {
                text += "\nDependencies: None\n"
            }

            d.detailsText = text
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xxlarge
        spacing: Theme.spacing.xlarge

        LogosText {
            text: "Package Manager"
            font.pixelSize: Theme.typography.titleText
            font.weight: Theme.typography.weightBold
            color: Theme.palette.text
        }

        LogosText {
            text: "Manage plugins and packages"
            font.pixelSize: Theme.typography.primaryText
            color: Theme.palette.textSecondary
        }

        RowLayout {
            spacing: Theme.spacing.small

            LogosButton {
                text: "Reload"
                enabled: !(d.backend && d.backend.isInstalling)
                onClicked: if (d.backend) d.backend.reload()
                implicitWidth: 100
                implicitHeight: 32
            }

            Button {
                id: installButton
                text: (d.backend && d.backend.isInstalling) ? "Installing..." : "Install"
                enabled: (d.backend && d.backend.hasSelectedPackages) && !(d.backend && d.backend.isInstalling)
                onClicked: if (d.backend) d.backend.install()

                contentItem: Row {
                    spacing: Theme.spacing.tiny + 2
                    anchors.centerIn: parent

                    Rectangle {
                        id: spinner
                        width: 14
                        height: 14
                        radius: 7
                        color: "transparent"
                        border.color: Theme.palette.text
                        border.width: 2
                        visible: (d.backend && d.backend.isInstalling)

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: Theme.palette.text
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 1
                        }

                        RotationAnimation on rotation {
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                            running: (d.backend && d.backend.isInstalling)
                        }
                    }

                    LogosText {
                        text: (d.backend && d.backend.isInstalling) ? "Installing..." : "Install"
                        font.pixelSize: Theme.typography.secondaryText
                        color: installButton.enabled ? Theme.palette.text : Theme.palette.textMuted
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                background: Rectangle {
                    implicitWidth: (d.backend && d.backend.isInstalling) ? 130 : 100
                    implicitHeight: 32
                    color: installButton.enabled
                           ? (installButton.pressed ? Theme.palette.successPressed : Theme.palette.success)
                           : Theme.palette.backgroundSecondary
                    radius: Theme.spacing.radiusSmall
                    border.color: installButton.enabled ? Theme.palette.successHover : Theme.palette.borderSubtle
                    border.width: 1

                    Behavior on implicitWidth {
                        NumberAnimation { duration: 150 }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            LogosText {
                text: "Release:"
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
                verticalAlignment: Text.AlignVCenter
            }

            ComboBox {
                id: releaseCombo
                model: d.backend ? d.backend.releases : []
                currentIndex: d.backend ? d.backend.selectedReleaseIndex : 0
                enabled: !(d.backend && (d.backend.isInstalling || d.backend.isLoading))
                Layout.minimumWidth: 200
                Layout.preferredWidth: Math.max(200, releaseMetrics.advanceWidth + 50)
                implicitHeight: 32

                TextMetrics {
                    id: releaseMetrics
                    font.pixelSize: Theme.typography.secondaryText
                    text: releaseCombo.displayText
                }

                displayText: (d.backend && d.backend.isLoading
                              && (d.backend.releases ? d.backend.releases.length : 0) <= 1)
                             ? "Loading releases…"
                             : currentText

                onActivated: function(index) {
                    if (d.backend) d.backend.pushSelectedReleaseIndex(index)
                }

                contentItem: LogosText {
                    leftPadding: 10
                    rightPadding: releaseCombo.indicator ? releaseCombo.indicator.width + 10 : 30
                    text: releaseCombo.displayText
                    font.pixelSize: Theme.typography.secondaryText
                    color: releaseCombo.enabled ? Theme.palette.text : Theme.palette.textMuted
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }

                background: Rectangle {
                    color: releaseCombo.enabled
                           ? (releaseCombo.pressed ? Theme.palette.pressed : Theme.palette.backgroundButton)
                           : Theme.palette.backgroundSecondary
                    radius: Theme.spacing.radiusSmall
                    border.color: releaseCombo.enabled ? Theme.palette.border : Theme.palette.borderSubtle
                    border.width: 1
                }

                popup: Popup {
                    y: releaseCombo.height + 2
                    width: releaseCombo.width
                    implicitHeight: contentItem.implicitHeight
                    padding: 1

                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: releaseCombo.popup.visible ? releaseCombo.delegateModel : null
                        currentIndex: releaseCombo.highlightedIndex
                    }

                    background: Rectangle {
                        color: Theme.palette.backgroundSecondary
                        border.color: Theme.palette.border
                        radius: Theme.spacing.radiusSmall
                    }
                }

                delegate: ItemDelegate {
                    width: releaseCombo.width
                    contentItem: LogosText {
                        text: modelData
                        color: Theme.palette.text
                        font.pixelSize: Theme.typography.secondaryText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    highlighted: releaseCombo.highlightedIndex === index
                    background: Rectangle {
                        color: highlighted ? Theme.palette.surface : "transparent"
                    }
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.preferredWidth: 100
                SplitView.minimumWidth: 150
                SplitView.maximumWidth: 250
                color: Theme.palette.backgroundSecondary

                ListView {
                    id: categoryList
                    anchors.fill: parent
                    model: d.backend ? d.backend.categories : []
                    currentIndex: d.backend ? d.backend.selectedCategoryIndex : 0

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 40
                        color: ListView.isCurrentItem
                               ? Theme.palette.surface
                               : (mouseArea.containsMouse ? Theme.palette.backgroundButton : "transparent")
                        radius: Theme.spacing.radiusSmall
                        enabled: !(d.backend && d.backend.isInstalling)

                        LogosText {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing.large
                            text: modelData
                            color: Theme.palette.text
                            font.pixelSize: Theme.typography.primaryText
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: if (d.backend) d.backend.pushSelectedCategoryIndex(index)
                            enabled: parent.enabled
                        }
                    }
                }
            }

            ColumnLayout {
                SplitView.fillWidth: true
                spacing: 0

                Rectangle {
                    id: packageListPane
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.6
                    color: Theme.palette.surface
                    border.color: Theme.palette.backgroundBlack
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.small
                        spacing: 0
                        opacity: (d.backend && d.backend.isLoading) ? 0.4 : 1.0
                        enabled: !(d.backend && d.backend.isLoading)

                        Rectangle {
                            Layout.fillWidth: true
                            height: 35
                            color: Theme.palette.surface

                            RowLayout {
                                anchors.fill: parent
                                spacing: 0

                                HeaderColumn {
                                    headerText: "S"
                                    centerAlign: true
                                    columnWidth: 40
                                }

                                HeaderColumn {
                                    headerText: "Package"
                                    columnWidth: 150
                                }

                                HeaderColumn {
                                    headerText: "Type"
                                    columnWidth: 80
                                }

                                HeaderColumn {
                                    headerText: "Description"
                                    fillWidth: true
                                }


                                HeaderColumn {
                                    headerText: "Status"
                                    columnWidth: 120
                                }

                                HeaderColumn {
                                    headerText: "Actions"
                                    columnWidth: 110
                                    centerAlign: true
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.palette.border
                        }

                        ListView {
                            id: packageList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: d.packagesModel

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 35
                                color: index % 2 === 0 ? Theme.palette.surface : Qt.darker(Theme.palette.surface, 1.15)
                                enabled: !(d.backend && d.backend.isInstalling)

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    Rectangle {
                                        Layout.preferredWidth: 40
                                        Layout.fillHeight: true
                                        color: "transparent"

                                        CheckBox {
                                            anchors.centerIn: parent
                                            // Selectable = NotInstalled OR Failed. Failed rows
                                            // need to be retryable — the point of a retry is
                                            // that a previous attempt left the package absent,
                                            // which from a "pick what to install" perspective
                                            // is the same as NotInstalled.
                                            property bool isDisabled: (model.isVariantAvailable !== true) ||
                                                                      ((model.installStatus | 0) !== PackageManagerUi.NotInstalled &&
                                                                       (model.installStatus | 0) !== PackageManagerUi.Failed)
                                            enabled: !isDisabled
                                            checked: model.isSelected === true
                                            onCheckedChanged: {
                                                if (checked !== model.isSelected) {
                                                    if (d.backend) d.backend.togglePackage(index, checked)
                                                }
                                            }

                                            indicator: Rectangle {
                                                implicitWidth: 18
                                                implicitHeight: 18
                                                x: parent.leftPadding
                                                y: parent.height / 2 - height / 2
                                                radius: Theme.spacing.radiusSmall
                                                color: parent.isDisabled
                                                       ? Theme.palette.backgroundMuted
                                                       : (parent.checked ? Theme.palette.info : Theme.palette.backgroundSecondary)
                                                border.color: parent.isDisabled
                                                              ? Theme.palette.borderSubtle
                                                              : (parent.checked ? Theme.palette.info : Theme.palette.border)

                                                LogosText {
                                                    anchors.centerIn: parent
                                                    text: "✓"
                                                    color: Theme.palette.text
                                                    font.pixelSize: Theme.typography.secondaryText
                                                    visible: parent.parent.checked
                                                }
                                            }
                                        }
                                    }

                                    DataCell {
                                        cellText: model.name || ""
                                        columnWidth: 150
                                    }

                                    DataCell {
                                        cellText: model.type || ""
                                        columnWidth: 80
                                    }

                                    DataCell {
                                        cellText: model.description || ""
                                        fillWidth: true
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 120
                                        Layout.fillHeight: true
                                        color: "transparent"

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 100
                                            height: 20
                                            radius: Theme.spacing.radiusSmall
                                            color: d.statusBgColor(model)
                                            border.color: d.statusBaseColor(model)
                                            border.width: 1

                                            LogosText {
                                                anchors.centerIn: parent
                                                text: d.statusText(model)
                                                color: d.statusTextColor(model)
                                                font.pixelSize: Theme.typography.secondaryText
                                            }
                                        }
                                    }

                                    // Actions column: per-row action button. Empty label
                                    // hides the button (rowActionLabel does the gating
                                    // on installStatus + installType).
                                    Rectangle {
                                        Layout.preferredWidth: 110
                                        Layout.fillHeight: true
                                        color: "transparent"

                                        Button {
                                            id: rowActionButton
                                            anchors.centerIn: parent
                                            readonly property string label: d.rowActionLabel(model)
                                            readonly property bool isUninstall: label === "Uninstall"
                                            visible: label !== ""
                                            enabled: visible && !(d.backend && d.backend.isInstalling)
                                            text: label
                                            onClicked: d.runRowAction(model, index)

                                            contentItem: Text {
                                                text: rowActionButton.text
                                                font.pixelSize: 11
                                                color: rowActionButton.enabled ? "#ffffff" : "#808080"
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            background: Rectangle {
                                                implicitWidth: 90
                                                implicitHeight: 22
                                                radius: 3
                                                color: rowActionButton.enabled
                                                       ? (rowActionButton.isUninstall
                                                            ? (rowActionButton.pressed ? "#7a1a1a" : "#a02020")
                                                            : (rowActionButton.pressed ? "#1a7f37" : "#238636"))
                                                       : "#2d2d2d"
                                                border.color: rowActionButton.enabled
                                                              ? (rowActionButton.isUninstall ? "#c94040" : "#2ea043")
                                                              : "#3d3d3d"
                                                border.width: 1
                                            }
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: if (d.backend) d.backend.requestPackageDetails(index)
                                    z: -1
                                }
                            }
                        }
                    }

                    // Loading spinner overlay (covers the package list pane only,
                    // so the category sidebar stays responsive).
                    Rectangle {
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        radius: 18
                        color: "transparent"
                        border.color: Theme.palette.text
                        border.width: 3
                        visible: d.backend && d.backend.isLoading

                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.palette.text
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 2
                        }

                        RotationAnimation on rotation {
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                            running: d.backend && d.backend.isLoading
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.palette.surface
                    border.color: Theme.palette.border
                    border.width: 1

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.small

                        TextArea {
                            text: d.detailsText
                            color: Theme.palette.text
                            readOnly: true
                            wrapMode: Text.Wrap
                            background: Rectangle {
                                color: "transparent"
                            }
                            font.family: Theme.typography.publicSans
                            font.pixelSize: Theme.typography.primaryText
                        }
                    }
                }
            }
        }
    }

    // Reusable header column component
    component HeaderColumn: Rectangle {
        property string headerText: ""
        property bool centerAlign: false
        property int columnWidth: 80
        property bool fillWidth: false
        
        Layout.preferredWidth: fillWidth ? -1 : columnWidth
        Layout.fillWidth: fillWidth
        Layout.fillHeight: true
        color: "transparent"

        LogosText {
            anchors.left: centerAlign ? undefined : parent.left
            anchors.leftMargin: centerAlign ? 0 : 10
            anchors.centerIn: centerAlign ? parent : undefined
            anchors.verticalCenter: centerAlign ? undefined : parent.verticalCenter
            text: headerText
            color: Theme.palette.textSecondary
            font.weight: Theme.typography.weightBold
            font.pixelSize: Theme.typography.secondaryText
        }
    }
    
    // Reusable data cell component
    component DataCell: Rectangle {
        property string cellText: ""
        property int columnWidth: 80
        property bool fillWidth: false
        
        Layout.preferredWidth: fillWidth ? -1 : columnWidth
        Layout.fillWidth: fillWidth
        Layout.fillHeight: true
        color: "transparent"

        LogosText {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: fillWidth ? parent.right : undefined
            anchors.rightMargin: fillWidth ? 10 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: cellText
            color: Theme.palette.text
            font.pixelSize: Theme.typography.primaryText
            elide: Text.ElideRight
            width: fillWidth ? undefined : (parent.width - 15)
        }
    }
}
