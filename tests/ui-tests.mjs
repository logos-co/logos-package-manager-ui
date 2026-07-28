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
  // Install is now a per-row action (ActionPill), not a top-bar button; the
  // always-present top-bar buttons are Reload and Manage Repositories.
  await app.expectTexts(["Reload", "Manage Repositories"]);
});

test("structure: table headers render", async (app) => {
  await waitForPmuiLoaded(app);
  // The old single "Status" column was split into per-row "Version" + "Action".
  await app.expectTexts(["Package", "Type", "Version", "Action", "Description"]);
});

test("store: exposes the documented properties with sane defaults", async (app) => {
  await waitForPmuiLoaded(app);

  // Values depend on fixture; only types and existence are pinned here.
  const isLoading = await storeProperty(app, "isLoading");
  const isInstalling = await storeProperty(app, "isInstalling");
  const pageSize = await storeProperty(app, "pageSize");
  const currentPage = await storeProperty(app, "currentPage");
  const repositoryCount = await storeProperty(app, "repositoryCount");

  if (typeof isLoading   !== "boolean") throw new Error(`isLoading not boolean: ${isLoading}`);
  if (typeof isInstalling !== "boolean") throw new Error(`isInstalling not boolean: ${isInstalling}`);
  if (typeof pageSize    !== "number")  throw new Error(`pageSize not number: ${pageSize}`);
  if (typeof currentPage !== "number")  throw new Error(`currentPage not number: ${currentPage}`);
  if (typeof repositoryCount !== "number") throw new Error(`repositoryCount not number: ${repositoryCount}`);
  if (currentPage < 1) throw new Error(`currentPage must be 1-indexed, got ${currentPage}`);
  if (repositoryCount < 0) throw new Error(`repositoryCount must be >= 0, got ${repositoryCount}`);
});

test("store: idle state — not installing once catalog has loaded", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
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

  // Wait for the catalog to finish loading before driving the tabs
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );

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
  // Use exact, unambiguous tab labels: bare "All" also matches the Types-sidebar
  // entry (a different-typed clickable the click router can't drive), so drive the
  // reset off the two unambiguous state tabs instead.
  await app.click("Not Installed", { exact: true });
  await app.click("Installed", { exact: true });
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

// The old top-bar bulk Install/Uninstall buttons (gated on
// hasInstallableSelection / hasUninstallableSelection) were replaced by a
// single dependency-aware per-row action flow. Those two booleans are gone;
// the surviving store-level invariants are runnableActionCount (drives the
// hidden "Run Actions (N)" button) and actionSummary (the per-category
// breakdown). With nothing selected, neither should report pending work.
test("actions: runnableActionCount is 0 with no selection", async (app) => {
  await waitForPmuiLoaded(app);
  const count = await storeProperty(app, "runnableActionCount");
  if (count !== 0) {
    throw new Error(`runnableActionCount=${count} on initial load (expected 0)`);
  }
});

test("actions: actionSummary reports no pending actions with no selection", async (app) => {
  await waitForPmuiLoaded(app);
  const summary = await storeProperty(app, "actionSummary");
  // actionSummary carries only non-zero category counts, so an empty/zero
  // total means nothing is queued.
  const total = summary && typeof summary === "object"
    ? Object.values(summary).reduce((a, b) => a + (Number(b) || 0), 0)
    : 0;
  if (total !== 0) {
    throw new Error(`actionSummary totals ${total} on initial load (expected 0): ${JSON.stringify(summary)}`);
  }
});

test("empty state: keyed on repositoryCount, not filtered totalCount", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );

  const repoCount = await storeProperty(app, "repositoryCount");
  if (typeof repoCount !== "number") {
    throw new Error(`repositoryCount type=${typeof repoCount}`);
  }

  if (repoCount === 0) {
    // No repos configured — empty state is the correct surface.
    await app.expectTexts(["No repositories configured"]);
    await app.expectTexts(["Add a package repository to browse and install plugins and modules."]);
    return;
  }

  // Repos are configured. Filtering the package list to 0 must NOT show
  // the "No repositories configured" empty state (that used to flicker
  // on category / type / install-state switches because it keyed off
  // filtered totalCount).
  const totalBefore = await storeProperty(app, "totalCount");
  const search = await app.findByProperty("placeholderText", "Search packages…");
  if (!search.matches || search.matches.length === 0) throw new Error("Search bar not found");
  const searchId = search.matches[0].id;

  await app.inspector.send("setProperty", {
    objectId: searchId, property: "text", value: "nothing-matches-this-zzzzz",
  });
  await app.waitFor(
    async () => {
      const t = await storeProperty(app, "totalCount");
      if (t !== 0) throw new Error(`expected 0 results, got ${t}`);
    },
    { timeout: 5000, interval: 250, description: "filter to return 0 results" }
  );

  const repoAfterFilter = await storeProperty(app, "repositoryCount");
  if (repoAfterFilter === 0) {
    throw new Error("repositoryCount must stay > 0 when only the package filter is empty");
  }
  // Package list chrome stays mounted (empty state would hide it).
  await app.expectTexts(["Package", "Type", "Version", "Action", "Description"]);

  // Restore so later tests start from a clean state.
  await app.inspector.send("setProperty", { objectId: searchId, property: "text", value: "" });
  await app.waitFor(
    async () => {
      const t = await storeProperty(app, "totalCount");
      if (t !== totalBefore) throw new Error(`expected ${totalBefore} after clear, got ${t}`);
    },
    { timeout: 5000, interval: 250, description: "totalCount to recover" }
  );
}, { skip: ["offscreen"] });

test("empty state: install-state filter switches do not drop repositoryCount", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );

  const repoBefore = await storeProperty(app, "repositoryCount");
  if (repoBefore === 0) return; // nothing to regress against

  await app.click("Not Installed", { exact: true });
  await app.click("Installed", { exact: true });
  await app.click("All", { exact: true });

  const repoAfter = await storeProperty(app, "repositoryCount");
  if (repoAfter !== repoBefore) {
    throw new Error(
      `repositoryCount changed during install-state filter switches: before=${repoBefore} after=${repoAfter}`
    );
  }
  await app.expectTexts(["Package", "Type", "Version", "Action", "Description"]);
}, { skip: ["offscreen"] });

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

// ─── Row-click regression tests ────────────────────────────────────
async function firstVisibleRowLabel(app) {
  const total = await storeProperty(app, "totalCount");
  if (!total || total === 0) return null;

  const store = await app.findByProperty("objectName", "pmui.BackendStore");
  if (!store.matches || store.matches.length === 0) return null;
  const storeId = store.matches[0].id;

  const res = await app.inspector.send("evaluate", {
    objectId: storeId,
    expression: `(function() {
      var m = packagesModel;
      if (!m || m.rowCount() === 0) return "";
      var rn = m.roleNames();
      var role = -1;
      for (var k in rn) {
        if (String(rn[k]) === "displayName") { role = parseInt(k); break; }
      }
      if (role < 0) return "";
      return String(m.data(m.index(0, 0), role) || "");
    })()`,
  });
  if (res.error) return null;
  const label = res.result;
  return (typeof label === "string" && label.length > 0) ? label : null;
}

test("row click: single click populates selectedPackageDetails", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 10000, interval: 500, description: "catalog to load" }
  );

  const label = await firstVisibleRowLabel(app);
  if (!label) return;   // empty fixture — nothing to click

  await app.click(label, { exact: true });
  await app.waitFor(
    async () => {
      const details = await storeProperty(app, "selectedPackageDetails");
      if (!details || !details.name) {
        throw new Error(`no details after single click: ${JSON.stringify(details)}`);
      }
    },
    { timeout: 5000, interval: 250,
      description: "selectedPackageDetails to populate on first click" }
  );
});

// ─── Categories sidebar scroll test ────────────────────────────────
test("categories sidebar: scrollable when contents overflow", async (app) => {
  await waitForPmuiLoaded(app);

  const sidebar = await app.findByProperty("objectName", "pmui.CategorySidebar");
  if (!sidebar.matches || sidebar.matches.length === 0) {
    throw new Error("CategorySidebar not found via objectName");
  }
  const sidebarId = sidebar.matches[0].id;

  const scroll = await app.findByProperty("objectName", "pmui.CategorySidebar.scrollArea");
  if (!scroll.matches || scroll.matches.length === 0) {
    throw new Error("CategorySidebar.scrollArea not found");
  }
  const scrollId = scroll.matches[0].id;

  const clip           = await propertyOf(app, scrollId, "clip");
  const height         = await propertyOf(app, scrollId, "height");
  const contentHeight  = await propertyOf(app, scrollId, "contentHeight");
  const overflowing    = await propertyOf(app, sidebarId, "overflowing");

  if (clip !== true) {
    throw new Error(`scrollArea.clip=${clip} (expected true — content must not bleed)`);
  }
  if (typeof height !== "number" || height <= 0) {
    throw new Error(`scrollArea.height=${height} (expected positive number)`);
  }
  if (typeof contentHeight !== "number" || contentHeight <= 0) {
    throw new Error(`scrollArea.contentHeight=${contentHeight} (expected positive number)`);
  }
  // Sanity: the "overflowing" alias mirrors the underlying gate.
  const expectedOverflow = contentHeight > height;
  if (overflowing !== expectedOverflow) {
    throw new Error(
      `overflowing=${overflowing} but contentHeight(${contentHeight}) > height(${height}) => ${expectedOverflow}`);
  }

  if (overflowing) {
    const targetY = Math.min(50, contentHeight - height);
    await app.inspector.send("setProperty", {
      objectId: scrollId, property: "contentY", value: targetY,
    });
    await app.waitFor(
      async () => {
        const y = await propertyOf(app, scrollId, "contentY");
        if (Math.abs(y - targetY) > 1) {
          throw new Error(`contentY=${y} (expected ~${targetY}) — sidebar didn't scroll`);
        }
      },
      { timeout: 2000, interval: 100, description: "sidebar to scroll to targetY" }
    );
    await app.inspector.send("setProperty", {
      objectId: scrollId, property: "contentY", value: 0,
    });
  }
});

test("row click after type filter: details.type matches the filtered type", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 10000, interval: 500, description: "catalog to load" }
  );

  // Pick a real Type (not "All") from the sidebar. Skip if the fixture
  // hasn't produced more than the "All" sentinel.
  const types = await storeProperty(app, "availableTypes");
  if (!Array.isArray(types) || types.length < 2) return;
  const chosenType = types[1];

  // The sidebar's Types entries are labelled by the type string itself.
  await app.click(chosenType, { exact: true });
  await app.waitFor(
    async () => {
      const idx = await storeProperty(app, "selectedTypeIndex");
      if (idx !== 1) throw new Error(`selectedTypeIndex=${idx} (expected 1)`);
    },
    { timeout: 5000, interval: 250, description: "type filter to apply" }
  );

  const label = await firstVisibleRowLabel(app);
  if (!label) return;   // no rows in this type — skip cleanly

  await app.click(label, { exact: true });
  await app.waitFor(
    async () => {
      const details = await storeProperty(app, "selectedPackageDetails");
      if (!details || !details.name) {
        throw new Error(`no details after click: ${JSON.stringify(details)}`);
      }
      if (details.type !== chosenType) {
        throw new Error(
          `details.type="${details.type}" (expected "${chosenType}") — ` +
          `clicked row belonged to a different type, backend acted on the ` +
          `raw model row instead of the filtered proxy row`);
      }
    },
    { timeout: 5000, interval: 250,
      description: "details to match the filtered type" }
  );
});

// ─── "Local" synthetic-repo tests ──────────────────────────────────
// PackageManagerBackend synthesises rows for installed packages the
// catalog doesn't publish. They carry repositoryUrl="" and
// repositoryDisplayName="Local", and sit in a section tier below all
// real repos. These tests assert the invariant without requiring a
// specific test fixture — they inspect the model in place.

async function inspectPackagesModel(app, expression) {
  const store = await app.findByProperty("objectName", "pmui.BackendStore");
  if (!store.matches || store.matches.length === 0) {
    throw new Error("BackendStore not found");
  }
  const storeId = store.matches[0].id;

  const res = await app.inspector.send("evaluate", {
    objectId: storeId,
    expression: `(function() {
      var m = packagesModel;
      if (!m) return null;
      ${expression}
    })()`,
  });
  // Surface eval errors verbatim — swallowing them into `null` made
  // every failure indistinguishable from "packagesModel is null".
  if (res.error) {
    throw new Error(`evaluate on packagesModel threw: ${res.error}`);
  }
  return res.result;
}

// Reset the store's filter state so a test can rely on the full model
// without inheriting whatever the previous test left behind (a type
// pick, a category, a search string). Uses the store's own setters —
// those go through push* over QtRO so the source-side proxy actually
// resets, not just the replica.
async function resetStoreFilters(app) {
  const store = await app.findByProperty("objectName", "pmui.BackendStore");
  if (!store.matches || store.matches.length === 0) return;
  const storeId = store.matches[0].id;
  await app.inspector.send("evaluate", {
    objectId: storeId,
    expression: `(function() {
      selectType(0);
      selectCategory(0);
      setSearchText("");
      setInstallStateFilter(0);
    })()`,
  });
}

// Read the backend-exposed role name → int map for `packagesModel`. Must
// go through `evaluate` + JSON — `getProperties` on a `property var` /
// QVariantMap yields the string "<QJSValue>" instead of the map's
// entries (QML's property system wraps var-typed values in QJSValue,
// which the inspector can't unpack). Evaluate returns actual JS values,
// and JSON.stringify guarantees a plain-string transport.
async function fetchPackageRoleIds(app) {
  const store = await app.findByProperty("objectName", "pmui.BackendStore");
  if (!store.matches || store.matches.length === 0) {
    throw new Error("BackendStore not found");
  }
  const storeId = store.matches[0].id;
  const res = await app.inspector.send("evaluate", {
    objectId: storeId,
    expression: `(function() {
      var src = backend ? backend.packageRoleIds : null;
      if (!src) return "";
      var out = {};
      for (var k in src) out[k] = src[k];
      return JSON.stringify(out);
    })()`,
  });
  if (res.error) throw new Error(`packageRoleIds eval threw: ${res.error}`);
  if (!res.result || typeof res.result !== "string") return null;
  try { return JSON.parse(res.result); } catch (_) { return null; }
}

test("local section: any empty-repositoryUrl row is labelled 'local'", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );
  await resetStoreFilters(app);
  const totalCount = await storeProperty(app, "totalCount");
  if (!totalCount || totalCount === 0) return;

  // roleNames() isn't Q_INVOKABLE on QAbstractItemModel, so we resolve
  // role IDs via the backend PROP exposed on the store as packageRoleIds
  // (populated once at backend construction from PackageListModel::roleNames).
  const roleIds = await fetchPackageRoleIds(app);
  if (!roleIds || typeof roleIds !== "object") {
    throw new Error(`packageRoleIds unavailable on BackendStore: ${JSON.stringify(roleIds)}`);
  }
  const urlRole  = roleIds.repositoryUrl;
  const dispRole = roleIds.repositoryDisplayName;
  const nameRole = roleIds.moduleName;
  if (typeof urlRole !== "number" || typeof dispRole !== "number") {
    throw new Error(
      "packageRoleIds is missing repositoryUrl / repositoryDisplayName: " +
      JSON.stringify(roleIds));
  }

  // Walk the whole packagesModel via QML JS — cheap even at fixture scale.
  // For every row where repositoryUrl is empty, the row MUST carry the
  // canonical lowercase "local" repositoryDisplayName. If no such row
  // exists the fixture has no local-only packages and the invariant is
  // vacuously satisfied (test returns "ok").
  const outcome = await inspectPackagesModel(app, `
    var URL = ${urlRole}, DISP = ${dispRole}, NAME = ${typeof nameRole === "number" ? nameRole : -1};
    var offenders = [];
    for (var i = 0; i < m.rowCount(); ++i) {
      var idx = m.index(i, 0);
      var url = String(m.data(idx, URL) || "");
      if (url.length > 0) continue;
      var disp = String(m.data(idx, DISP) || "");
      if (disp !== "local") {
        var name = NAME >= 0 ? String(m.data(idx, NAME) || "") : "";
        offenders.push(name + ": '" + disp + "'");
      }
    }
    return offenders.length === 0 ? "ok" : "bad:" + offenders.join(",");
  `);

  if (outcome === null) throw new Error("packagesModel is null on BackendStore");
  if (outcome !== "ok") {
    throw new Error("empty-repo rows with wrong repositoryDisplayName: " + outcome);
  }
});

test("local section: 'No repositories configured' hides when local rows exist", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => {
      const loading = await storeProperty(app, "isLoading");
      if (loading) throw new Error("still loading");
    },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );

  const repoCount = await storeProperty(app, "repositoryCount");
  const totalCount = await storeProperty(app, "totalCount");
  // Empty-state CTA is visible iff repositoryCount === 0 AND totalCount === 0.
  // Any other combination should not surface the CTA — verify by absence of
  // its subtitle text.
  if (repoCount > 0 || totalCount > 0) {
    const res = await app.findByProperty(
      "text", "Add a package repository to browse and install plugins and modules.");
    if (res.matches && res.matches.length > 0) {
      for (const m of res.matches) {
        try {
          const visible = await propertyOf(app, m.id, "visible");
          if (visible) {
            throw new Error(
              `empty-state CTA visible with repositoryCount=${repoCount} ` +
              `and totalCount=${totalCount} — expected hidden`);
          }
        } catch (e) {
          if (String(e.message).startsWith("empty-state CTA")) throw e;
          // property missing on that match — skip
        }
      }
    }
  }
});

// ─── Picker-driven size/date change ────────────────────────────────
// setRowVersion mirrors the picked version's catalog size/releasedAt
// into pkg["size"]/pkg["dateUpdated"] via rowaction::applyPickedSizeAndDate.
// Guard: moving the picker on a multi-version package must update at
// least one of size/dateUpdated. Skips cleanly if no fixture package has
// multiple versions (offscreen / empty catalog).
test("picker: switching version updates size/dateUpdated", async (app) => {
  await waitForPmuiLoaded(app);
  await app.waitFor(
    async () => { if (await storeProperty(app, "isLoading")) throw new Error("loading"); },
    { timeout: 20000, interval: 500, description: "catalog to finish loading" }
  );
  // Empty fixture on offscreen CI → skip. Same reason as the local-section
  // test above.
  await resetStoreFilters(app);
  const totalCount = await storeProperty(app, "totalCount");
  if (!totalCount || totalCount === 0) return;

  const roleIds = await fetchPackageRoleIds(app);
  if (!roleIds || typeof roleIds !== "object") {
    throw new Error(`packageRoleIds unavailable on BackendStore: ${JSON.stringify(roleIds)}`);
  }
  const sizeRole   = roleIds.size;
  const dateRole   = roleIds.dateUpdated;
  const avRole     = roleIds.availableVersions;
  const selVerRole = roleIds.selectedVersionIndex;
  if (typeof sizeRole !== "number" || typeof dateRole !== "number" ||
      typeof avRole !== "number" || typeof selVerRole !== "number") {
    throw new Error(
      "packageRoleIds missing size/dateUpdated/availableVersions/selectedVersionIndex: " +
      JSON.stringify(roleIds));
  }

  const store = await app.findByProperty("objectName", "pmui.BackendStore");
  if (!store.matches || store.matches.length === 0) throw new Error("BackendStore not found");
  const storeId = store.matches[0].id;

  async function evalOnStore(expression, label) {
    const res = await app.inspector.send("evaluate", { objectId: storeId, expression });
    if (res.error) throw new Error(`${label}: evaluate threw: ${res.error}`);
    return res.result;
  }

  // Pick a row where availableVersions[0] and [1] DIFFER in size or date.
  // Skipping fixture-degenerate rows here — where two versions coincidentally
  // share both fields — is what makes this test non-flaky: the assertion at
  // the bottom can only prove the wiring works if the picked row's versions
  // produce observably different values.
  const initial = await evalOnStore(`(function() {
      var m = packagesModel;
      if (!m) return "no-model";
      var AV = ${avRole}, SIZE = ${sizeRole}, DATE = ${dateRole};
      for (var i = 0; i < m.rowCount(); ++i) {
        var idx = m.index(i, 0);
        var av  = m.data(idx, AV);
        if (!av || av.length < 2) continue;
        var v0 = av[0] || {}, v1 = av[1] || {};
        if (String(v0.size) === String(v1.size) &&
            String(v0.releasedAt) === String(v1.releasedAt)) continue;
        return String(i) + "|" +
               String(m.data(idx, SIZE) || "") + "|" +
               String(m.data(idx, DATE) || "");
      }
      return "no-usable-row";
    })()`, "initial sample");
  if (initial === "no-usable-row") return;   // no distinguishable multi-version row
  if (typeof initial !== "string" || initial.startsWith("no-")) {
    throw new Error(`could not sample initial state: ${initial}`);
  }
  const [rowStr, size0, date0] = initial.split("|");
  const rowIndex = parseInt(rowStr, 10);

  // Move the picker to index 1.
  await evalOnStore(`setRowVersion(${rowIndex}, 1)`, "setRowVersion(1)");

  await app.waitFor(async () => {
    const idx = await evalOnStore(
      `packagesModel.data(packagesModel.index(${rowIndex}, 0), ${selVerRole})`,
      "selectedVersionIndex poll");
    if (Number(idx) !== 1) throw new Error(`selectedVersionIndex=${idx}, expected 1`);
  }, { timeout: 5000, interval: 100, description: "setRowVersion to propagate to replica" });

  const after = await evalOnStore(`(function() {
      var m = packagesModel;
      var idx = m.index(${rowIndex}, 0);
      return String(m.data(idx, ${sizeRole}) || "") + "|" +
             String(m.data(idx, ${dateRole}) || "");
    })()`, "post-switch sample");
  const [size1, date1] = String(after).split("|");

  // Restore (fire-and-forget — no other test observes this row's picker).
  await evalOnStore(`setRowVersion(${rowIndex}, 0)`, "setRowVersion(0)");

  // The write landed (gate above proved it). If size/date still match v0
  // the row-build → applyPickedSizeAndDate wiring is broken.
  if (size0 === size1 && date0 === date1) {
    throw new Error(
      `setRowVersion landed (selectedVersionIndex=1) but size/date didn't ` +
      `update: v0 size=${size0} date=${date0} · v1 read-back size=${size1} date=${date1}. ` +
      `applyPickedSizeAndDate in PackageListModel::setRowVersion is broken.`);
  }
});

run();
