import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.Icons
import Logos.Layouts


// Table header: the install-state tabs, and beside them while they fit,
// under them when not, the toolbar: the category and type pickers
// (while the sidebar that holds them is hidden), the filter, Install Local
// Package, Reload and Manage Repositories, which fold into one ⋯ when they
// don't fit, as Alisher's ADR-064 has them.
//
// The bulk button is now ONE: Install + Uninstall got merged into a
// single user-chosen-per-row plan that fires from "Run Actions". The
// previous two-button arrangement enabled both buttons whenever a mixed
// selection (some installed + some not) was active, and each silently
// acted on its own subset — the original source of the "what's this
// going to do?" confusion. Uninstall is now per-row only (kebab ⋮).
LogosItemLayout {
    id: root
    objectName: "pmui.tableHeader"

    property bool isInstalling: false
    property bool isLoading: false
    // Number of selected rows that resolve to a runnable RowAction
    // (anything other than NoOp / NotAvailable). Drives the
    // "Run Actions (N)" button label and enabled state. Bound via
    // BackendStore from PackageManagerBackend::refreshActionSummary().
    property int runnableActionCount: 0
    // { install, upgrade, downgrade, reinstall, retry } — non-zero
    // counts only. Forwarded to the confirm-summary popup so the user
    // sees exactly what's about to run before "Run Actions" fires.
    property var actionSummary: ({})

    // Install-state tab: 0 = All, 1 = Installed, 2 = Not Installed. "All" is
    // all install states — availability is the separate filter below, so it
    // stays accurate.
    property int stateIndex: 0
    readonly property alias stateTabs: tabs

    // Availability filter. False (the default) lists only what this host can
    // install; the filter menu is where the rest can be asked for.
    property bool showUnavailable: false
    signal showUnavailableRequested(bool show)

    // The sidebar's filters as pickers, while the sidebar is hidden.
    property bool showFilterPickers: false
    property var categories: []
    property int categoryIndex: 0
    property var types: []
    property int typeIndex: 0
    signal categorySelected(int index)
    signal typeSelected(int index)

    signal reloadClicked()
    // Click on "Install Local Package". The parent QML owns the file picker —
    // this signal only says the user asked for one.
    signal installLocalClicked()
    // Click on the new bulk button. The parent QML is responsible for
    // opening the confirm-summary popup (driven by `actionSummary`) and
    // calling `BackendStore.runSelectedActions()` on confirm; this
    // signal does NOT execute anything directly.
    signal runActionsClicked()
    signal stateRequested(int state)
    // Requests navigation to basecamp Settings → Repositories.
    signal repositoriesClicked()

    spacing: Theme.spacing.large
    // The tabs keep their line before the toolbar goes under them.
    minimumContentWidth: tabs.implicitWidth

    // ─── Install-state tabs, at their own width: a tab bar given more shares
    // it out, given less squeezes. The page's title already says what they list.
    Item {
        implicitWidth: tabs.implicitWidth
        implicitHeight: tabs.implicitHeight

        LogosTabBar {
            id: tabs
            width: implicitWidth
            spacing: Theme.spacing.large

            currentIndex: root.stateIndex
            onCurrentIndexChanged: {
                if (currentIndex !== root.stateIndex)
                    root.stateRequested(currentIndex)
            }

            LogosTabButton { text: qsTr("All"); iconSource: LogosIcons.pages }
            LogosTabButton { text: qsTr("Installed") }
            LogosTabButton { text: qsTr("Not Installed") }
        }
    }

    // ─── Toolbar: the pickers, then the actions, folding into ⋯ ───
    meta: [
        LogosComboBox {
            id: categoryCombo
            objectName: "pmui.categoryCombo"
            visible: root.showFilterPickers
            Layout.fillWidth: true
            // Its own text at least, so the line keeps it whole and the ⋯
            // takes only what is left.
            Layout.minimumWidth: Math.max(Theme.size.touchTarget * 2, Math.min(implicitWidth, Layout.maximumWidth))
            Layout.maximumWidth: 220
            model: root.categories
            currentIndex: root.categoryIndex
            displayText: currentIndex <= 0 ? qsTr("All categories") : currentText
            onActivated: function(index) { root.categorySelected(index) }
        },

        LogosComboBox {
            id: typeCombo
            objectName: "pmui.typeCombo"
            visible: root.showFilterPickers && root.types.length > 1
            Layout.fillWidth: true
            Layout.minimumWidth: Math.max(Theme.size.touchTarget * 2, Math.min(implicitWidth, Layout.maximumWidth))
            Layout.maximumWidth: 180
            model: root.types
            currentIndex: root.typeIndex
            displayText: currentIndex <= 0 ? qsTr("All types") : currentText
            onActivated: function(index) { root.typeSelected(index) }
        },

        LogosOverflowRow {
            id: actions
            objectName: "pmui.toolbarActions"
            Layout.fillWidth: true
            Layout.minimumWidth: Theme.size.touchTarget

            LogosIconButton {
                id: filterButton
                objectName: "pmui.filterButton"
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Theme.size.controlLarge
                Layout.preferredHeight: Theme.size.controlLarge
                size: Theme.size.controlLarge
                iconSize: Theme.size.iconSmall
                iconSource: Qt.resolvedUrl("../images/filter.svg")
                iconColor: root.showUnavailable
                           ? Theme.palette.primary
                           : Theme.palette.text
                onClicked: filterMenu.popupUnder(filterButton)

                background: Rectangle {
                    radius: Theme.spacing.radiusLarge
                    color: (root.showUnavailable || filterButton.hovered
                            || filterButton.pressed || filterButton.activeFocus)
                           ? Theme.palette.backgroundMuted
                           : "transparent"
                }

                LogosToolTip {
                    text: root.showUnavailable
                          ? qsTr("Filters: showing unavailable")
                          : qsTr("Filter packages")
                    placement: LogosToolTip.Bottom
                    visible: filterButton.hovered && !filterMenu.visible
                }
            }

            LogosMenu {
                id: filterMenu
                objectName: "pmui.filterMenu"

                ShowUnavailableItem {
                    objectName: "pmui.showUnavailableItem"
                }
            }

            LogosButton {
                id: installLocalBtn
                objectName: "pmui.installLocalButton"
                Layout.preferredWidth: 180
                Layout.preferredHeight: Theme.size.controlLarge
                radius: Theme.spacing.radiusLarge
                text: qsTr("Install Local Package")
                leadingIcon.source: LogosIcons.install
                leadingIcon.size: 18
                // An install running leaves this, and Manage Repositories: a
                // local install waits its turn behind it, and the repositories
                // are Basecamp's Settings. Reload waits for it to end.
                enabled: !root.isLoading
                onClicked: root.installLocalClicked()
            }

            LogosButton {
                id: reloadBtn
                objectName: "pmui.reloadButton"
                Layout.preferredWidth: 130
                Layout.preferredHeight: Theme.size.controlLarge
                radius: Theme.spacing.radiusLarge
                text: qsTr("Reload")
                leadingIcon.source: LogosIcons.refresh
                leadingIcon.size: 18
                enabled: !root.isInstalling && !root.isLoading
                onClicked: root.reloadClicked()
            }

            // Multi-repo: open the Manage Repositories popup.
            LogosButton {
                objectName: "pmui.repositoriesButton"
                Layout.preferredWidth: 150
                Layout.preferredHeight: Theme.size.controlLarge
                radius: Theme.spacing.radiusLarge
                text: qsTr("Manage Repositories")
                onClicked: root.repositoriesClicked()
            }

            // Single bulk-action button. Currently hidden — the bulk
            // selection / confirm-summary surface is off (see the
            // RunActionsConfirm comment block in PackageManager.qml), and the
            // backend's bulk path is a stub.
            LogosButton {
                visible: false
                Layout.preferredWidth: 160
                Layout.preferredHeight: Theme.size.controlLarge
                radius: Theme.spacing.radiusLarge
                text: root.runnableActionCount > 0
                      ? qsTr("Run Actions (%1)").arg(root.runnableActionCount)
                      : qsTr("Run Actions")
                enabled: root.runnableActionCount > 0 && !root.isInstalling
                onClicked: root.runActionsClicked()
            }

            // Folded, the same actions in a menu.
            menu: LogosMenu {
                objectName: "pmui.moreMenu"

                ShowUnavailableItem {
                    objectName: "pmui.moreShowUnavailableItem"
                }

                LogosMenuSeparator {}

                LogosMenuItem {
                    objectName: "pmui.moreInstallLocalItem"
                    text: qsTr("Install Local Package…")
                    enabled: !root.isLoading
                    onTriggered: root.installLocalClicked()
                }

                LogosMenuItem {
                    objectName: "pmui.moreReloadItem"
                    text: qsTr("Reload")
                    enabled: !root.isInstalling && !root.isLoading
                    onTriggered: root.reloadClicked()
                }

                LogosMenuItem {
                    objectName: "pmui.moreRepositoriesItem"
                    text: qsTr("Manage Repositories…")
                    onTriggered: root.repositoriesClicked()
                }
            }
        }
    ]

    // "Show unavailable", with the design system's check mark: in the filter
    // menu, and in ⋯ once the toolbar folds.
    component ShowUnavailableItem: LogosMenuItem {
        id: showUnavailableItem
        readonly property int indicatorSize: 14
        text: qsTr("Show unavailable")
        checkable: true
        checked: root.showUnavailable
        onTriggered: root.showUnavailableRequested(checked)

        indicator: LogosIcon {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing.medium
            width: showUnavailableItem.indicatorSize
            height: showUnavailableItem.indicatorSize
            visible: showUnavailableItem.checked
            source: LogosIcons.check
            color: Theme.palette.text
        }

        contentItem: LogosText {
            leftPadding: Theme.spacing.medium
                         + showUnavailableItem.indicatorSize
                         + Theme.spacing.small
            rightPadding: Theme.spacing.medium
            verticalAlignment: Text.AlignVCenter
            text: showUnavailableItem.text
            color: showUnavailableItem.enabled
                   ? Theme.palette.text
                   : Theme.palette.textMuted
        }
    }
}
