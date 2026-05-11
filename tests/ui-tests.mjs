#!/usr/bin/env node
// Usage:
//   node tests/ui-tests.mjs                   # against a running app
//   node tests/ui-tests.mjs --ci <binary>     # launch, test, kill
//   node tests/ui-tests.mjs <substring>       # filter tests by name
//
// Requires: nix build .#test-framework -o result-mcp

import { resolve } from "node:path";

const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

// Reload is the most stable mount signal — always present, always rendered.
async function waitForPmuiLoaded(app, timeout = 15000) {
  await app.waitFor(
    async () => { await app.expectTexts(["Reload"]); },
    { timeout, interval: 500, description: "Package Manager UI to load" }
  );
}

// findByType doesn't match QML-declared types (Qt mangles them as
// <Type>_QMLTYPE_<n>). Anchor lookups on the QObject objectName instead —
// BackendStore.qml sets objectName: "pmui.BackendStore".
async function storeProperty(app, propName) {
  const res = await app.findByProperty("objectName", "pmui.BackendStore");
  if (res.error || !res.matches || res.matches.length === 0) {
    throw new Error('No object found with objectName "pmui.BackendStore"');
  }
  return propertyOf(app, res.matches[0].id, propName);
}

async function propertyOf(app, objectId, propName) {
  const res = await app.getProperties(objectId);
  if (res.error) throw new Error(`getProperties failed: ${res.error}`);
  const prop = res.properties.find((p) => p.name === propName);
  if (!prop) throw new Error(`property "${propName}" not found`);
  return prop.value;
}

test("smoke: PMUI loads and shows title", async (app) => {
  await waitForPmuiLoaded(app);
  await app.expectTexts(["Package Manager"]);
});

test("smoke: subtitle renders", async (app) => {
  await waitForPmuiLoaded(app);
  await app.expectTexts(["Manage your plugins and packages."]);
});

test("smoke: top-bar action labels render", async (app) => {
  await waitForPmuiLoaded(app);
  // Uninstall is gated on selection, may not always be visible.
  await app.expectTexts(["Reload", "Install"]);
});

test("structure: table headers render", async (app) => {
  await waitForPmuiLoaded(app);
  await app.expectTexts(["Package", "Type", "Description", "Status"]);
});

test("store: exposes the documented properties with sane defaults", async (app) => {
  await waitForPmuiLoaded(app);

  // Values depend on fixture; only types and existence are pinned here.
  const isLoading = await storeProperty(app, "isLoading");
  const isInstalling = await storeProperty(app, "isInstalling");
  const pageSize = await storeProperty(app, "pageSize");
  const currentPage = await storeProperty(app, "currentPage");

  if (typeof isLoading   !== "boolean") throw new Error(`isLoading not boolean: ${isLoading}`);
  if (typeof isInstalling !== "boolean") throw new Error(`isInstalling not boolean: ${isInstalling}`);
  if (typeof pageSize    !== "number")  throw new Error(`pageSize not number: ${pageSize}`);
  if (typeof currentPage !== "number")  throw new Error(`currentPage not number: ${currentPage}`);
  if (currentPage < 1) throw new Error(`currentPage must be 1-indexed, got ${currentPage}`);
});

test("store: idle state — not installing once catalog has loaded", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 10000, interval: 500, description: "catalog to finish loading" }
  );
  const isInstalling = await storeProperty(app, "isInstalling");
  if (isInstalling) throw new Error("isInstalling should be false at idle");
});

test("search: typing into the search bar updates store.searchText", async (app) => {
  await waitForPmuiLoaded(app);

  const search = await app.findByProperty("placeholderText", "Search packages…");
  if (!search.matches || search.matches.length === 0) {
    throw new Error("Search bar not found");
  }
  const searchId = search.matches[0].id;

  // Drive via setProperty rather than typing — typing depends on the focus chain.
  await app.inspector.send("setProperty", {
    objectId: searchId, property: "text", value: "waku",
  });

  await app.waitFor(
    async () => {
      const t = await storeProperty(app, "searchText");
      if (t !== "waku") throw new Error(`searchText="${t}" (expected "waku")`);
    },
    { timeout: 5000, interval: 250, description: "store.searchText to mirror search bar" }
  );
}, { skip: ["offscreen"] });

test("search: clearing search resets totalCount to its pre-filter value", async (app) => {
  await waitForPmuiLoaded(app);

  const totalBefore = await storeProperty(app, "totalCount");

  const search = await app.findByProperty("placeholderText", "Search packages…");
  const searchId = search.matches[0].id;

  await app.inspector.send("setProperty", { objectId: searchId, property: "text", value: "nothing-matches-this-zzzzz" });
  await app.waitFor(
    async () => {
      const t = await storeProperty(app, "totalCount");
      // A fixture happening to contain this row is a fixture issue, not a test bug.
      if (t !== 0) throw new Error(`expected 0 results, got ${t}`);
    },
    { timeout: 5000, interval: 250, description: "filter to apply" }
  );

  await app.inspector.send("setProperty", { objectId: searchId, property: "text", value: "" });
  await app.waitFor(
    async () => {
      const t = await storeProperty(app, "totalCount");
      if (t !== totalBefore) throw new Error(`expected ${totalBefore} after clear, got ${t}`);
    },
    { timeout: 5000, interval: 250, description: "totalCount to recover" }
  );
}, { skip: ["offscreen"] });

test("filter tabs: All/Installed/Not Installed labels render", async (app) => {
  await waitForPmuiLoaded(app);
  await app.expectTexts(["All", "Installed", "Not Installed"]);
});

test("filter tabs: clicking 'Not Installed' updates store.installStateFilter", async (app) => {
  await waitForPmuiLoaded(app);

  // Use exact matching: substring would let "Installed" match the "Not Installed"
  // tab, and the bare "All" matches the sidebar's Types entry before the tab.
  await app.click("Not Installed", { exact: true });
  await app.waitFor(
    async () => {
      const v = await storeProperty(app, "installStateFilter");
      if (v !== 2) throw new Error(`installStateFilter=${v} (expected 2)`);
    },
    { timeout: 5000, interval: 250, description: "filter state to switch" }
  );

  await app.click("Installed", { exact: true });
  await app.waitFor(
    async () => {
      const v = await storeProperty(app, "installStateFilter");
      if (v !== 1) throw new Error(`installStateFilter=${v} (expected 1)`);
    },
    { timeout: 5000, interval: 250, description: "filter to switch to Installed" }
  );
});

test("paginator: currentPage starts at 1 and is bounded by totalCount", async (app) => {
  await waitForPmuiLoaded(app);

  const total = await storeProperty(app, "totalCount");
  const pageSize = await storeProperty(app, "pageSize");
  const currentPage = await storeProperty(app, "currentPage");

  if (currentPage < 1) throw new Error(`currentPage must be 1-indexed, got ${currentPage}`);

  // Paginator is only visible when totalCount > 0; otherwise the bound check is moot.
  if (total > 0) {
    const maxPage = Math.max(1, Math.ceil(total / pageSize));
    if (currentPage > maxPage) {
      throw new Error(`currentPage=${currentPage} exceeds maxPage=${maxPage}`);
    }
  }
});

test("paginator: applying a filter resets currentPage to 1", async (app) => {
  await waitForPmuiLoaded(app);

  // PagingProxy resets currentPage on source-model reset, which a filter change triggers.
  await app.click("All");
  await app.click("Installed");
  await app.waitFor(
    async () => {
      const p = await storeProperty(app, "currentPage");
      if (p !== 1) throw new Error(`currentPage=${p} (expected 1 after filter change)`);
    },
    { timeout: 5000, interval: 250, description: "currentPage to reset to 1" }
  );
});

test("sort: clicking a column header updates store.sortRole", async (app) => {
  await waitForPmuiLoaded(app);

  await app.click("Type");
  await app.waitFor(
    async () => {
      const role = await storeProperty(app, "sortRole");
      if (role !== "type") throw new Error(`sortRole="${role}" (expected "type")`);
    },
    { timeout: 5000, interval: 250, description: "sortRole to update" }
  );
}, { skip: ["offscreen"] });

test("details panel: hidden by default (no selection)", async (app) => {
  await waitForPmuiLoaded(app);

  // DetailsPanel may still be in the tree but invisible; check `visible` per match
  // rather than asserting absence from the tree.
  const res = await app.findByProperty("text", "Details");
  if (res.matches && res.matches.length > 0) {
    let anyVisible = false;
    for (const m of res.matches) {
      try {
        const visible = await propertyOf(app, m.id, "visible");
        if (visible) { anyVisible = true; break; }
      } catch { /* property missing — skip */ }
    }
    if (anyVisible) {
      throw new Error("DetailsPanel header visible without a selected row");
    }
  }
});

test("bulk install: button is initially disabled (no selection)", async (app) => {
  await waitForPmuiLoaded(app);
  const has = await storeProperty(app, "hasInstallableSelection");
  if (has !== false) {
    throw new Error(`hasInstallableSelection=${has} on initial load (expected false)`);
  }
});

test("bulk uninstall: button is initially disabled (no selection)", async (app) => {
  await waitForPmuiLoaded(app);
  const has = await storeProperty(app, "hasUninstallableSelection");
  if (has !== false) {
    throw new Error(`hasUninstallableSelection=${has} on initial load (expected false)`);
  }
});

test("reload: clicking the reload button triggers a refresh cycle", async (app) => {
  await waitForPmuiLoaded(app);

  // reloadCatalog is a no-op stub for empty fixtures; we only assert the click
  // doesn't error and the app settles back to not-loading.
  await app.click("Reload");
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 15000, interval: 500, description: "refresh cycle to settle" }
  );
});

test("regression: store.totalCount and pageSize are integers, not strings", async (app) => {
  // Old QtRO bug serialised int props as strings on macOS, silently breaking ceil arithmetic.
  await waitForPmuiLoaded(app);
  const total = await storeProperty(app, "totalCount");
  const pageSize = await storeProperty(app, "pageSize");
  if (typeof total !== "number")    throw new Error(`totalCount type=${typeof total}`);
  if (typeof pageSize !== "number") throw new Error(`pageSize type=${typeof pageSize}`);
});

test("regression: availableTypes always includes 'All' as the first entry", async (app) => {
  // The type sidebar must show 'All' even before packages load.
  await waitForPmuiLoaded(app);
  const types = await storeProperty(app, "availableTypes");
  if (!Array.isArray(types) || types.length === 0) {
    throw new Error(`availableTypes is empty: ${JSON.stringify(types)}`);
  }
  if (types[0] !== "All") {
    throw new Error(`availableTypes[0]="${types[0]}" (expected "All")`);
  }
});

test("regression: sortOrder is one of Qt.AscendingOrder/DescendingOrder", async (app) => {
  await waitForPmuiLoaded(app);
  const order = await storeProperty(app, "sortOrder");
  if (order !== 0 && order !== 1) {
    throw new Error(`sortOrder=${order} (expected 0 or 1)`);
  }
});

run();
