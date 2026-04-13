import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

            function statusBgColor(row) {
                if (row.isVariantAvailable !== true) return "#4d3a1a"
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return "#5c4a1a"
                if (s === PackageManagerUi.Installed) return "#2d5016"
                if (s === PackageManagerUi.Failed) return "#5c1a1a"
                if (s === PackageManagerUi.UpgradeAvailable) return "#1a3f5c"
                if (s === PackageManagerUi.DowngradeAvailable) return "#3d1a5c"
                if (s === PackageManagerUi.DifferentHash) return "#5c3d1a"
                return "#4d4d4d"
            }

            function statusBorderColor(row) {
                if (row.isVariantAvailable !== true) return "#8B6914"
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return "#C9A227"
                if (s === PackageManagerUi.Installed) return "#4CAF50"
                if (s === PackageManagerUi.Failed) return "#C62828"
                if (s === PackageManagerUi.UpgradeAvailable) return "#4A90E2"
                if (s === PackageManagerUi.DowngradeAvailable) return "#8E4AE2"
                if (s === PackageManagerUi.DifferentHash) return "#E28E4A"
                return "#666666"
            }

            function statusTextColor(row) {
                if (row.isVariantAvailable !== true) return "#C9A227"
                var s = row.installStatus | 0
                if (s === PackageManagerUi.Installing) return "#E6C547"
                if (s === PackageManagerUi.Installed) return "#8BC34A"
                if (s === PackageManagerUi.Failed) return "#EF5350"
                if (s === PackageManagerUi.UpgradeAvailable) return "#7ab8ff"
                if (s === PackageManagerUi.DowngradeAvailable) return "#C084FF"
                if (s === PackageManagerUi.DifferentHash) return "#FFB870"
                return "#999999"
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
    }

    color: "#1e1e1e"

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
            }
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
        anchors.margins: 40
        spacing: 20

        Text {
            text: "Package Manager"
            font.pixelSize: 24
            font.bold: true
            color: "#ffffff"
        }

        Text {
            text: "Manage plugins and packages"
            font.pixelSize: 14
            color: "#a0a0a0"
        }

        RowLayout {
            spacing: 10

            Button {
                text: "Reload"
                enabled: !(d.backend && d.backend.isInstalling)
                onClicked: if (d.backend) d.backend.reload()

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 13
                    color: parent.enabled ? "#ffffff" : "#808080"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 100
                    implicitHeight: 32
                    color: parent.enabled ? (parent.pressed ? "#3d3d3d" : "#4d4d4d") : "#2d2d2d"
                    radius: 4
                    border.color: parent.enabled ? "#5d5d5d" : "#3d3d3d"
                    border.width: 1
                }
            }

            Button {
                text: (d.backend && d.backend.isInstalling) ? "Installing..." : "Install"
                enabled: (d.backend && d.backend.hasSelectedPackages) && !(d.backend && d.backend.isInstalling)
                onClicked: if (d.backend) d.backend.install()

                contentItem: Row {
                    spacing: 6
                    anchors.centerIn: parent
                    
                    Rectangle {
                        id: spinner
                        width: 14
                        height: 14
                        radius: 7
                        color: "transparent"
                        border.color: "#ffffff"
                        border.width: 2
                        visible: (d.backend && d.backend.isInstalling)
                        
                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: "#ffffff"
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
                    
                    Text {
                        text: (d.backend && d.backend.isInstalling) ? "Installing..." : "Install"
                        font.pixelSize: 13
                        color: parent.parent.enabled ? "#ffffff" : "#808080"
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                background: Rectangle {
                    implicitWidth: (d.backend && d.backend.isInstalling) ? 130 : 100
                    implicitHeight: 32
                    color: parent.enabled ? (parent.pressed ? "#1a7f37" : "#238636") : "#2d2d2d"
                    radius: 4
                    border.color: parent.enabled ? "#2ea043" : "#3d3d3d"
                    border.width: 1
                    
                    Behavior on implicitWidth {
                        NumberAnimation { duration: 150 }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "Release:"
                color: "#a0a0a0"
                font.pixelSize: 13
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
                    font.pixelSize: 13
                    text: releaseCombo.displayText
                }

                displayText: (d.backend && d.backend.isLoading
                              && (d.backend.releases ? d.backend.releases.length : 0) <= 1)
                             ? "Loading releases…"
                             : currentText

                onActivated: function(index) {
                    if (d.backend) d.backend.pushSelectedReleaseIndex(index)
                }

                contentItem: Text {
                    leftPadding: 10
                    rightPadding: releaseCombo.indicator ? releaseCombo.indicator.width + 10 : 30
                    text: releaseCombo.displayText
                    font.pixelSize: 13
                    color: releaseCombo.enabled ? "#ffffff" : "#808080"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }

                background: Rectangle {
                    color: releaseCombo.enabled
                           ? (releaseCombo.pressed ? "#3d3d3d" : "#4d4d4d")
                           : "#2d2d2d"
                    radius: 4
                    border.color: releaseCombo.enabled ? "#5d5d5d" : "#3d3d3d"
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
                        color: "#2d2d2d"
                        border.color: "#5d5d5d"
                        radius: 4
                    }
                }

                delegate: ItemDelegate {
                    width: releaseCombo.width
                    contentItem: Text {
                        text: modelData
                        color: "#ffffff"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    highlighted: releaseCombo.highlightedIndex === index
                    background: Rectangle {
                        color: highlighted ? "#3d3d3d" : "transparent"
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
                color: "#2d2d2d"

                ListView {
                    id: categoryList
                    anchors.fill: parent
                    model: d.backend ? d.backend.categories : []
                    currentIndex: d.backend ? d.backend.selectedCategoryIndex : 0

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 40
                        color: ListView.isCurrentItem ? "#3d3d3d" : (mouseArea.containsMouse ? "#353535" : "transparent")
                        radius: 3
                        enabled: !(d.backend && d.backend.isInstalling)

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            text: modelData
                            color: "#ffffff"
                            font.pixelSize: 14
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
                    color: "#333333"
                    border.color: "#000000"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 0
                        opacity: (d.backend && d.backend.isLoading) ? 0.4 : 1.0
                        enabled: !(d.backend && d.backend.isLoading)

                        Rectangle {
                            Layout.fillWidth: true
                            height: 35
                            color: "#333333"

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
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: "#444444"
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
                                color: index % 2 === 0 ? "#333333" : "#2a2a2a"
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
                                            property bool isDisabled: (model.isVariantAvailable !== true) ||
                                                                      ((model.installStatus | 0) !== PackageManagerUi.NotInstalled)
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
                                                radius: 3
                                                color: parent.isDisabled ? "#3d3d3d" : (parent.checked ? "#4A90E2" : "#2d2d2d")
                                                border.color: parent.isDisabled ? "#4d4d4d" : (parent.checked ? "#4A90E2" : "#5d5d5d")

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "✓"
                                                    color: "#ffffff"
                                                    font.pixelSize: 12
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
                                            radius: 3
                                            color: d.statusBgColor(model)
                                            border.color: d.statusBorderColor(model)
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: d.statusText(model)
                                                color: d.statusTextColor(model)
                                                font.pixelSize: 11
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
                        border.color: "#ffffff"
                        border.width: 3
                        visible: d.backend && d.backend.isLoading

                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: "#ffffff"
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
                    color: "#333333"
                    border.color: "#444444"
                    border.width: 1

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 10

                        TextArea {
                            text: d.detailsText
                            color: "#ffffff"
                            readOnly: true
                            wrapMode: Text.Wrap
                            background: Rectangle {
                                color: "transparent"
                            }
                            font.pixelSize: 13
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
        
        Text {
            anchors.left: centerAlign ? undefined : parent.left
            anchors.leftMargin: centerAlign ? 0 : 10
            anchors.centerIn: centerAlign ? parent : undefined
            anchors.verticalCenter: centerAlign ? undefined : parent.verticalCenter
            text: headerText
            color: "#a0a0a0"
            font.bold: true
            font.pixelSize: 12
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
        
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: fillWidth ? parent.right : undefined
            anchors.rightMargin: fillWidth ? 10 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: cellText
            color: "#ffffff"
            font.pixelSize: 13
            elide: Text.ElideRight
            width: fillWidth ? undefined : (parent.width - 15)
        }
    }
}
