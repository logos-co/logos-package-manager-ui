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
            
            // Status
            var status = details.installStatus
            if (status === PackageManagerUi.Installed) {
                text += "Status: Installed\n"
            } else if (status === PackageManagerUi.Installing) {
                text += "Status: Installing…\n"
            } else if (status === PackageManagerUi.Failed) {
                text += "Status: Failed\n"
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
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.6
                    color: "#333333"
                    border.color: "#000000"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 0

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
                                    columnWidth: 90
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
                                                                      ((model.installStatus | 0) === PackageManagerUi.Installed) ||
                                                                      ((model.installStatus | 0) === PackageManagerUi.Installing)
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
                                        Layout.preferredWidth: 90
                                        Layout.fillHeight: true
                                        color: "transparent"

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 80
                                            height: 20
                                            radius: 3
                                            color: !(model.isVariantAvailable === true) ? "#4d3a1a" :
                                                   ((model.installStatus | 0) === PackageManagerUi.Installing ? "#5c4a1a" :
                                                   ((model.installStatus | 0) === PackageManagerUi.Installed ? "#2d5016" :
                                                   ((model.installStatus | 0) === PackageManagerUi.Failed ? "#5c1a1a" : "#4d4d4d")))
                                            border.color: !(model.isVariantAvailable === true) ? "#8B6914" :
                                                          ((model.installStatus | 0) === PackageManagerUi.Installing ? "#C9A227" :
                                                          ((model.installStatus | 0) === PackageManagerUi.Installed ? "#4CAF50" :
                                                          ((model.installStatus | 0) === PackageManagerUi.Failed ? "#C62828" : "#666666")))
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: !(model.isVariantAvailable === true) ? "Not Available" :
                                                      ((model.installStatus | 0) === PackageManagerUi.Installing ? "Installing…" :
                                                      ((model.installStatus | 0) === PackageManagerUi.Installed ? "Installed" :
                                                      ((model.installStatus | 0) === PackageManagerUi.Failed ? "Failed" : "Not Installed")))
                                                color: !(model.isVariantAvailable === true) ? "#C9A227" :
                                                       ((model.installStatus | 0) === PackageManagerUi.Installing ? "#E6C547" :
                                                       ((model.installStatus | 0) === PackageManagerUi.Installed ? "#8BC34A" :
                                                       ((model.installStatus | 0) === PackageManagerUi.Failed ? "#EF5350" : "#999999")))
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
