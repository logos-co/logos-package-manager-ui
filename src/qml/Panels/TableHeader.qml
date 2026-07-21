import QtQuick
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls
import Logos.Icons


// Table header: title "Packages" + install-state tabs on the left,
// reload icon / Manage Repositories / "Run Actions (N)" on the right.
//
// The bulk button is now ONE: Install + Uninstall got merged into a
// single user-chosen-per-row plan that fires from "Run Actions". The
// previous two-button arrangement enabled both buttons whenever a mixed
// selection (some installed + some not) was active, and each silently
// acted on its own subset — the original source of the "what's this
// going to do?" confusion. Uninstall is now per-row only (kebab ⋮).
GridLayout {
    id: root

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

    // Install-state tab: 0 = All, 1 = Installed, 2 = Not Installed.
    property int stateIndex: 0
    readonly property alias stateTabs: tabs

    signal reloadClicked()
    // Click on the new bulk button. The parent QML is responsible for
    // opening the confirm-summary popup (driven by `actionSummary`) and
    // calling `BackendStore.runSelectedActions()` on confirm; this
    // signal does NOT execute anything directly.
    signal runActionsClicked()
    signal stateRequested(int state)
    // Requests navigation to basecamp Settings → Repositories.
    signal repositoriesClicked()

    columnSpacing: Theme.spacing.large
    rowSpacing: Theme.spacing.medium
    columns: (leftHalf.implicitWidth + rightHalf.implicitWidth + columnSpacing) <= width
             ? 2 : 1

    // ─── Left half: title + install-state tabs ───
    RowLayout {
        id: leftHalf
        Layout.fillWidth: true
        spacing: Theme.spacing.large

        LogosText {
            text: qsTr("Packages")
            font.pixelSize: Theme.typography.panelTitleText
            font.weight: Theme.typography.weightMedium
            color: Theme.palette.text
        }

        LogosTabBar {
            id: tabs
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

        Item { Layout.fillWidth: true }
    }

    // ─── Right half: release picker + bulk actions ───
    RowLayout {
        id: rightHalf
        Layout.fillWidth: true
        spacing: Theme.spacing.medium

        Item { Layout.fillWidth: root.columns === 2 }

        LogosButton {
            id: reloadBtn
            objectName: "pmui.reloadButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.preferredWidth: 130
            Layout.maximumWidth: 130
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Reload")
            icon.source:LogosIcons.refresh
            icon.size: 18
            enabled: !root.isInstalling && !root.isLoading
            onClicked: root.reloadClicked()
        }

        // Multi-repo: open the Manage Repositories popup.
        LogosButton {
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            Layout.preferredWidth: 150
            Layout.maximumWidth: 150
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: qsTr("Manage Repositories")
            enabled: !root.isInstalling
            onClicked: root.repositoriesClicked()
        }

        // Single bulk-action button. Currently hidden — the bulk
        // selection / confirm-summary surface is off (see the
        // RunActionsConfirm comment block in PackageManager.qml).
        // Kept compiled so re-enabling is a one-line `visible: true`
        // away once the dep-aware per-row flow stabilises.
        LogosButton {
            visible: false
            Layout.fillWidth: true
            Layout.minimumWidth: 130
            Layout.preferredWidth: 160
            Layout.maximumWidth: 180
            Layout.preferredHeight: 40
            radius: Theme.spacing.radiusLarge
            text: root.runnableActionCount > 0
                  ? qsTr("Run Actions (%1)").arg(root.runnableActionCount)
                  : qsTr("Run Actions")
            enabled: root.runnableActionCount > 0 && !root.isInstalling
            onClicked: root.runActionsClicked()
        }
    }
}
