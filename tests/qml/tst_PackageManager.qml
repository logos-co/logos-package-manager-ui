import QtQuick
import QtTest

import Logos.Theme
import Logos.PackageManagerUi 1.0
import PmuiFakes

import "../../src/qml"
import "FitChecks.js" as FitChecks

// The Package Manager on the design system, from a phone to a desktop, as
// Alisher's ADR-064 has it: the categories and types in a sidebar while the
// list keeps its width, pickers in its toolbar otherwise; the toolbar's
// actions folding into ⋯; the table dropping columns, then cards; a
// package's details beside the list or, on a phone, the page. Nothing checks
// a width class to get there: the design system's layouts measure.
TestCase {
    id: testCase
    name: "PackageManager"
    width: 1280
    height: 800
    // Input can't reach anything in an invisible TestCase, the default.
    visible: true
    when: windowShown

    // The host: `logos` hands the view its backend and model, here a fake.
    property var backend: null
    property var logos: QtObject {
        signal intentRequested(string requestId, string intent, var params, string requesterName)
        property var requests: []
        property var requestParams: []
        // What the host answers a request with; null leaves it unanswered.
        property var answer: null
        function module(name) { return testCase.backend }
        function model(name, property, prefetch) { return testCase.backend ? testCase.backend.model : null }
        function respond(id, ok, data, error) {}
        function request(intent, params, callback) {
            requests = requests.concat([intent])
            requestParams = requestParams.concat([params])
            if (answer)
                callback(answer)
        }
    }

    Component { id: fakeComp; FakeBackend {} }
    Component { id: viewComp; PackageManager {} }

    function init() {
        failOnWarning(/Binding loop/)
        backend = fakeComp.createObject(testCase)
        logos.requests = []
        logos.requestParams = []
        logos.answer = null
    }

    function cleanup() {
        Theme.viewWidth = 0
        backend.destroy()
        backend = null
    }

    // Told its width as a host tells its view: a phone-wide view gets the
    // phone's type and touch sizes.
    function makeView(w) {
        Theme.viewWidth = w
        const view = createTemporaryObject(viewComp, testCase, { width: w, height: testCase.height })
        verify(view)
        waitForRendering(view)
        return view
    }

    function find(view, name) {
        const item = findChild(view, name)
        verify(item, name)
        return item
    }

    // Opens `name`'s details, from its row or, on a phone, its card. Found
    // through the list, not by objectName: after a filter, the old cards
    // linger until deleted.
    function openDetails(view, name) {
        const list = find(view, "pmui.packageList")
        const rows = list.showingCards ? list.cardsView : list.view
        const index = backend.shown.findIndex(r => r.name === name)
        verify(index >= 0, name + " is listed")
        rows.positionViewAtIndex(index, ListView.Contain)
        let row = null
        tryVerify(() => (row = rows.itemAtIndex(index)) !== null, 2000, "a row for " + name)
        waitForRendering(view)
        mouseClick(row, Theme.spacing.large * 2, row.height / 2)
        waitForRendering(view)
    }

    // The table's columns left at its width, as LogosTable's own tests read them.
    function shownTitles(list) {
        return list.d.shownColumns.map(column => column.title)
    }

    function openMore(view) {
        const actions = find(view, "pmui.toolbarActions")
        verify(actions.folded)
        mouseClick(actions.moreButtonItem)
        tryCompare(actions.menu, "opened", true)
        // Until laid out, the menu's items sit on top of each other.
        waitForRendering(testCase)
        return actions.menu
    }

    // ── Nothing past the sides ──

    function test_fits_data() {
        const out = []
        for (const w of [360, 390, 600, 700, 900, 1280])
            for (const details of [false, true])
                out.push({ tag: w + (details ? " with details" : ""), width: w, details: details })
        return out
    }

    function test_fits(data) {
        const view = makeView(data.width)
        if (data.details) {
            openDetails(view, "waku_module")
            verify(find(view, "pmui.detailsPanel").panelOpen)
        }
        const over = FitChecks.overflow(view)
        compare(over.map(item => FitChecks.describe(item, view)).join("; "), "", "past the sides")
    }

    // ── Desktop ──

    // The categories in the sidebar, the toolbar's actions as buttons, the
    // search a field beside the title; Description dropped rather than
    // scroll sideways.
    function test_desktop() {
        const view = makeView(1280)
        verify(find(view, "pmui.body").sideShown)
        verify(find(view, "pmui.CategorySidebar").visible)
        verify(!find(view, "pmui.categoryCombo").visible)
        verify(!find(view, "pmui.toolbarActions").folded)
        verify(find(view, "pmui.reloadButton").visible)
        verify(find(view, "pmui.filterButton").visible)
        verify(!find(view, "pmui.header").searchItem.folds)
        const list = find(view, "pmui.packageList")
        verify(!list.showingCards)
        const titles = shownTitles(list)
        verify(titles.indexOf("Description") < 0, "Description goes first: " + titles)
        verify(titles.indexOf("Released") >= 0, titles)
        verify(list.d.totalMinWidth <= list.width, "no sideways scroll")
    }

    // A tap on a row opens its details beside the list; their ✕ closes them.
    function test_desktop_details_beside_the_list() {
        const view = makeView(1280)
        openDetails(view, "waku_module")
        const panel = find(view, "pmui.detailsPanel")
        verify(panel.panelOpen)
        verify(panel.beside)
        verify(panel.contentPaneItem.visible, "the list stays")
        mouseClick(find(view, "pmui.DetailsPanel.close"))
        verify(!panel.panelOpen)
        verify(!view.detailOpen, "closing clears the pick")
    }

    // The details look as they did: a raised card headed "Details", a space
    // from the list and no hairline, where they had become a flat panel.
    function test_the_details_are_the_card_they_were() {
        const view = makeView(1280)
        openDetails(view, "waku_module")
        const panel = find(view, "pmui.detailsPanel")
        const card = find(view, "pmui.DetailsPanel")
        compare(String(card.color), String(Theme.palette.surfaceRaised))
        compare(card.radius, Theme.spacing.radiusXlarge)
        verify(!panel.dividerItem.visible)
        compare(panel.contentPaneItem.width, panel.width - panel.panelWidth - Theme.spacing.medium)
        verify(!panel.headerItem.visible, "no bar of the side panel's own")
    }

    // ── Narrower ──

    // No sidebar: Category and Type are pickers in the toolbar, and its
    // actions fold into ⋯ (ADR-064).
    function test_narrower_pickers_and_more() {
        const view = makeView(700)
        verify(!find(view, "pmui.body").sideShown)
        verify(!find(view, "pmui.CategorySidebar").visible)
        verify(find(view, "pmui.categoryCombo").visible)
        verify(find(view, "pmui.typeCombo").visible)
        verify(find(view, "pmui.toolbarActions").folded)
        verify(!find(view, "pmui.reloadButton").enabled, "the folded buttons are out of reach")
        const list = find(view, "pmui.packageList")
        verify(!list.showingCards)
        const titles = shownTitles(list)
        for (const kept of ["Package", "Available", "Action"])
            verify(titles.indexOf(kept) >= 0, kept + " stays: " + titles)
        verify(list.d.totalMinWidth <= list.width, "no sideways scroll")
    }

    function test_a_picker_filters_as_the_sidebar_did() {
        const view = makeView(700)
        const combo = find(view, "pmui.categoryCombo")
        compare(combo.displayText, "All categories")
        combo.activated(2)
        compare(backend.selectedCategoryIndex, 2)
        compare(combo.displayText, "Storage")
        verify(backend.shown.every(r => r.category === "Storage"))
        const types = find(view, "pmui.typeCombo")
        types.activated(1)
        compare(backend.selectedTypeIndex, 1)
    }

    function textsIn(item) {
        let out = []
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.visible && typeof child.text === "string")
                out.push(child.text)
            out = out.concat(textsIn(child))
        }
        return out
    }

    // The list's header is its tabs, with no "Packages" heading before them:
    // the page's title already says what they list.
    function test_the_list_header_starts_with_its_tabs_data() {
        return [{ tag: "phone", width: 390 }, { tag: "desktop", width: 1280 }]
    }

    function test_the_list_header_starts_with_its_tabs(data) {
        const view = makeView(data.width)
        const header = find(view, "pmui.tableHeader")
        const tabs = header.stateTabs
        compare(tabs.mapToItem(header, 0, 0).x, 0)
        compare(tabs.width, tabs.implicitWidth, "the tabs at their own width")
        verify(!textsIn(header).includes("Packages"), "no heading")
    }

    // On a phone the page is "Packages", what its details go back to, with no
    // subtitle; wider, "Package Manager" and its subtitle.
    function test_a_phone_page_is_packages_data() {
        return [{ tag: "phone", width: 390, phone: true }, { tag: "desktop", width: 1280, phone: false }]
    }

    function test_a_phone_page_is_packages(data) {
        const view = makeView(data.width)
        const header = find(view, "pmui.header")
        compare(header.titleItem.text, data.phone ? "Packages" : "Package Manager")
        compare(header.subtitleItem.visible, !data.phone)
    }

    // On a phone the pickers keep their text whole: the ⋯ takes what is
    // left of their line, or a line of its own, never a share of theirs.
    function test_the_pickers_keep_their_text_data() {
        return [{ tag: "390", width: 390, oneLine: true }, { tag: "360", width: 360, oneLine: false }]
    }

    function test_the_pickers_keep_their_text(data) {
        const view = makeView(data.width)
        const categories = find(view, "pmui.categoryCombo")
        const types = find(view, "pmui.typeCombo")
        verify(categories.visible && types.visible)
        verify(categories.width >= categories.implicitWidth, "categories " + categories.width + " for " + categories.implicitWidth)
        verify(types.width >= types.implicitWidth, "types " + types.width + " for " + types.implicitWidth)
        const more = find(view, "pmui.toolbarActions")
        const header = find(view, "pmui.tableHeader")
        compare(more.mapToItem(header, more.width, 0).x, header.width, "the ⋯ at the line's end")
        if (data.oneLine)
            compare(more.mapToItem(header, 0, 0).y, categories.mapToItem(header, 0, 0).y, "on the pickers' line")
    }

    // ⋯ holds what the toolbar did: the filter, Install Local Package,
    // Reload, Manage Repositories.
    function test_more_does_what_the_toolbar_did() {
        const view = makeView(700)
        let menu = openMore(view)
        mouseClick(findChild(menu, "pmui.moreReloadItem"))
        compare(backend.reloads, 1)

        menu = openMore(view)
        mouseClick(findChild(menu, "pmui.moreShowUnavailableItem"))
        verify(backend.showUnavailable)

        menu = openMore(view)
        verify(findChild(menu, "pmui.moreShowUnavailableItem").checked)
        mouseClick(findChild(menu, "pmui.moreRepositoriesItem"))
        compare(logos.requests, ["basecamp.repositories.manage"])
    }

    // An install running leaves Install Local Package and Manage
    // Repositories usable: a local install waits its turn, and the
    // repositories are Basecamp's Settings. Reload waits for it to end.
    function test_an_install_running_leaves_the_toolbar_usable() {
        const view = makeView(1280)
        backend.isInstalling = true
        verify(find(view, "pmui.installLocalButton").enabled)
        verify(find(view, "pmui.repositoriesButton").enabled)
        verify(!find(view, "pmui.reloadButton").enabled)
    }

    function test_an_install_running_leaves_more_usable() {
        const view = makeView(700)
        backend.isInstalling = true
        const menu = openMore(view)
        verify(findChild(menu, "pmui.moreInstallLocalItem").enabled)
        verify(findChild(menu, "pmui.moreRepositoriesItem").enabled)
        verify(!findChild(menu, "pmui.moreReloadItem").enabled)
    }

    // ── Phone ──

    // A card per package, grouped by repository; a tap opens its details as
    // the page, under "‹ Packages", which goes back to the list: a page you
    // go back from, as Settings' sections are, not a card you close. That row
    // heads the page, as Settings' does: no page title over it, and no
    // "Details" or ✕ under it.
    function test_phone_cards_open_details_as_the_page() {
        const view = makeView(390)
        const list = find(view, "pmui.packageList")
        verify(list.showingCards)
        const card = find(view, "pmui.card.chat_ui")
        compare(card.title, "Chat UI")
        verify(card.subtitle.indexOf("v0.3.1 installed") >= 0, card.subtitle)
        verify(card.showChevron)

        openDetails(view, "waku_module")
        const panel = find(view, "pmui.detailsPanel")
        verify(panel.showingPanel)
        verify(!panel.contentPaneItem.visible, "the details are the page")
        compare(find(view, "pmui.DetailsPanel").width, panel.width)
        verify(panel.headerItem.visible, "a bar at the top")
        compare(panel.headerItem.title, "Packages", "it names where back goes")
        const back = panel.headerItem.backButtonItem
        verify(back.visible, "‹")
        verify(!find(view, "pmui.header").visible, "no page title over the way back")
        fuzzyCompare(panel.headerItem.mapToItem(view, 0, 0).y, Theme.pageMargin(390), 0.5, "it heads the page")
        verify(!find(view, "pmui.DetailsPanel.title").visible, "no \"Details\" under it")
        verify(!find(view, "pmui.DetailsPanel.close").visible, "no ✕")
        mouseClick(back)
        verify(!panel.panelOpen)
        verify(panel.contentPaneItem.visible)
        verify(!view.detailOpen)
        verify(find(view, "pmui.header").visible, "the page's title is back")
    }

    // Beside cards (a tablet with details open) they are a card you close.
    function test_details_beside_cards_close_with_their_x() {
        const view = makeView(700)
        openDetails(view, "waku_module")
        const panel = find(view, "pmui.detailsPanel")
        verify(panel.beside)
        verify(!panel.headerItem.visible, "no bar")
        verify(find(view, "pmui.header").visible)
        verify(find(view, "pmui.DetailsPanel.title").visible, "the card's title")
        const close = find(view, "pmui.DetailsPanel.close")
        verify(close.visible)
        mouseClick(close)
        verify(!panel.panelOpen)
    }

    // `name`'s card, scrolled into view; found through the list, as the old
    // cards linger after a filter until deleted.
    function cardFor(view, name) {
        const cards = find(view, "pmui.packageList").cardsView
        const index = backend.shown.findIndex(r => r.name === name)
        verify(index >= 0, name + " is listed")
        cards.positionViewAtIndex(index, ListView.Contain)
        let card = null
        tryVerify(() => (card = cards.itemAtIndex(index)) !== null, 2000, "a card for " + name)
        waitForRendering(view)
        const row = findChild(card, "pmui.card." + name)
        verify(row, "the card's row")
        return row
    }

    // Every card ends alike, its action, ⋯ and ›: a trash on only the ones
    // that can be uninstalled left the rest ragged.
    function test_phone_cards_end_alike() {
        const view = makeView(390)
        const ends = []
        for (const name of ["waku_module", "chat_module", "accounts_module"]) {
            const card = cardFor(view, name)
            const action = findChild(card, "pmui.ActionPill")
            ends.push(Math.round(action.mapToItem(card, action.width, 0).x))
            verify(card.menuButtonItem.visible, name + " has ⋯")
        }
        compare(ends[1], ends[0], "uninstallable or not, the action ends where the others' do")
        compare(ends[2], ends[0], "built in or not")
        compare(findChild(view, "pmui.cardUninstall.waku_module"), null, "no trash")
    }

    // A card's ⋯, as Settings' cards have: its details, and Uninstall where it
    // can run, which asks Basecamp to uninstall that package.
    function test_a_cards_menu_uninstalls_where_it_can() {
        const view = makeView(390)
        logos.answer = { ok: true, data: {} }
        let card = cardFor(view, "waku_module")
        mouseClick(card.menuButtonItem)
        tryCompare(card.menu, "opened", true)
        // Until laid out, the menu's items sit on top of each other.
        waitForRendering(testCase)
        const uninstall = findChild(card.menu, "pmui.cardMenu.uninstall")
        verify(uninstall.visible)
        mouseClick(uninstall)
        compare(logos.requests, ["basecamp.packages.confirm_uninstall"])
        compare(backend.uninstalls, ["waku_module"])
        tryCompare(card.menu, "visible", false)
        verify(!findChild(cardFor(view, "chat_module").menu, "pmui.cardMenu.uninstall").visible,
               "not installed")
        verify(!findChild(cardFor(view, "accounts_module").menu, "pmui.cardMenu.uninstall").visible,
               "built in")
    }

    // A long press opens the menu rather than the details; from it, Open
    // details does.
    function test_a_cards_menu_opens_on_a_long_press_and_opens_the_details() {
        const view = makeView(390)
        const card = cardFor(view, "chat_ui")
        mousePress(card, card.width / 3, card.height / 2)
        tryCompare(card.menu, "opened", true, 3000)
        mouseRelease(card, card.width / 3, card.height / 2)
        waitForRendering(testCase)
        verify(!view.detailOpen, "a long press is not a tap")
        mouseClick(findChild(card.menu, "pmui.cardMenu.details"))
        tryVerify(() => view.detailOpen)
        compare(find(view, "pmui.DetailsPanel").details.name, "chat_ui")
    }

    // The search is a 🔍 at the end of the title's line, not a row of its
    // own; open, it filters the cards.
    function test_phone_search_is_on_the_title_line() {
        const view = makeView(390)
        const header = find(view, "pmui.header")
        const search = header.searchItem
        verify(search.folds && !search.expanded)
        mouseClick(search.openButtonItem)
        tryVerify(() => search.searchBarItem.textInput.activeFocus)
        for (const c of "wal")
            keyClick(c)
        compare(backend.searchText, "wal")
        compare(backend.shown.length, 2)
    }

    // Another app asking to show a package fills the search with its name.
    function test_showing_a_package_searches_for_it() {
        const view = makeView(1280)
        logos.intentRequested("r1", "packages.show", { name: "Wallet" }, "chat")
        compare(find(view, "pmui.header").searchText, "Wallet")
        compare(backend.searchText, "Wallet")
    }

    // ── A package's own actions, in its details ──

    // Where the list shows no table beside them, a package's details end
    // with what its row had: the version picker, the action and Uninstall,
    // and the action's tooltip as a line, as nothing hovers on a phone.
    function test_phone_details_end_with_the_rows_actions() {
        const view = makeView(390)
        openDetails(view, "waku_module")
        const card = find(view, "pmui.DetailsPanel")
        const actions = find(view, "pmui.DetailsPanel.actions")
        verify(actions.visible)
        compare(Math.round(actions.mapToItem(card, 0, actions.height).y), card.height - Theme.spacing.medium,
                "last in the card")
        compare(find(view, "pmui.DetailsPanel.version").displayText, "0.4.2")
        compare(find(view, "pmui.DetailsPanel.action").action, PackageManagerUi.NoOp)
        verify(find(view, "pmui.DetailsPanel.uninstall").visible)
        compare(find(view, "pmui.DetailsPanel.actionHelp").text, "v0.4.2 installed")
    }

    function test_details_beside_cards_carry_the_actions() {
        const view = makeView(700)
        openDetails(view, "waku_module")
        verify(find(view, "pmui.detailsPanel").beside)
        verify(find(view, "pmui.packageList").showingCards)
        verify(find(view, "pmui.DetailsPanel.actions").visible)
    }

    // Beside a table, its row has them: the card stays as it was.
    function test_desktop_details_leave_the_actions_to_the_row() {
        const view = makeView(1280)
        openDetails(view, "waku_module")
        verify(find(view, "pmui.detailsPanel").panelOpen)
        compare(findChild(view, "pmui.DetailsPanel.actions"), null)
    }

    // Uninstall only where the package can go: not one that isn't installed,
    // nor a built-in one.
    function test_details_offer_uninstall_only_where_it_can_be_done() {
        const view = makeView(390)
        openDetails(view, "chat_module")
        verify(!find(view, "pmui.DetailsPanel.uninstall").visible, "not installed")
        compare(find(view, "pmui.DetailsPanel.actionHelp").text, "Installs v0.4.2")
        mouseClick(find(view, "pmui.detailsPanel").headerItem.backButtonItem)
        find(view, "pmui.categoryCombo").activated(5)
        tryVerify(() => findChild(view, "pmui.card.accounts_module") !== null, 2000)
        openDetails(view, "accounts_module")
        verify(!find(view, "pmui.DetailsPanel.uninstall").visible, "built in")
    }

    // Uninstall asks Basecamp to uninstall the package the details show,
    // wherever the list has moved its row since they opened.
    function test_details_uninstall_the_package_they_show() {
        const view = makeView(390)
        logos.answer = { ok: true, data: {} }
        openDetails(view, "waku_module")
        const opened = backend.shown.findIndex(r => r.name === "waku_module")
        backend.pushSortRole("size")
        verify(backend.shown[opened].name !== "waku_module", "the rows moved")
        mouseClick(find(view, "pmui.DetailsPanel.uninstall"))
        compare(logos.requests, ["basecamp.packages.confirm_uninstall"])
        compare(logos.requestParams[0].names, ["waku_module"])
        compare(backend.uninstalls, ["waku_module"])
    }

    function test_the_details_action_runs_on_their_package() {
        const view = makeView(390)
        openDetails(view, "chat_module")
        const pill = find(view, "pmui.DetailsPanel.action")
        compare(pill.action, PackageManagerUi.Install)
        mouseClick(pill)
        compare(logos.requests, ["basecamp.packages.confirm_install"])
        compare(logos.requestParams[0].name, "chat_module")
    }

    // A version picked in the details moves the row, and the details, read
    // again, describe it.
    function test_a_version_picked_in_the_details_moves_the_row() {
        const view = makeView(390)
        openDetails(view, "chat_ui")
        const picker = find(view, "pmui.DetailsPanel.version")
        compare(picker.count, 2)
        picker.activated(1)
        compare(backend.versionPicks, [[backend.shown.findIndex(r => r.name === "chat_ui"), 1]])
        compare(picker.displayText, "0.3.1")
        compare(find(view, "pmui.DetailsPanel").details.version, "0.3.1")
    }

    // An install that runs while the details are open shows in them: the
    // action as it goes, then the details, read again, once it lands.
    function test_the_details_follow_their_package_as_it_installs() {
        const view = makeView(390)
        openDetails(view, "chat_module")
        const card = find(view, "pmui.DetailsPanel")
        const pill = find(view, "pmui.DetailsPanel.action")
        backend.setRow("chat_module", { installStatus: PackageManagerUi.Installing })
        verify(pill.installing)
        compare(card.details.installStatus, PackageManagerUi.Installing)
        backend.setRow("chat_module", { installStatus: PackageManagerUi.Installed,
                                        installedVersion: "0.4.2", rowAction: PackageManagerUi.NoOp })
        compare(card.details.installStatus, PackageManagerUi.Installed)
        verify(find(view, "pmui.DetailsPanel.uninstall").visible, "the user's now: it can go again")
    }

    // ── The test's own stub ──

    // The enums the views read are the backend's: the stub these tests run
    // on says what src/package_manager_ui.rep says.
    function test_the_enum_stub_matches_the_backend() {
        const xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../src/package_manager_ui.rep"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "the .rep is readable (QML_XHR_ALLOW_FILE_READ=1)")
        const pattern = /ENUM\s+\w+\s*\{([^}]*)\}/g
        let count = 0
        for (let m; (m = pattern.exec(xhr.responseText)) !== null; ) {
            for (const entry of m[1].split(",")) {
                const pair = entry.replace(/\/\/.*$/mg, "").split("=")
                if (pair.length !== 2)
                    continue
                const name = pair[0].trim(), value = Number(pair[1].trim())
                compare(PackageManagerUi[name], value, "PackageManagerUi." + name)
                ++count
            }
        }
        verify(count > 30, "read the .rep's enums")
    }
}
