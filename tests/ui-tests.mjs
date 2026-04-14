#!/usr/bin/env node
// package_manager_ui integration tests
//
// Usage:
//   node tests/ui-tests.mjs                   # run against a running app
//   node tests/ui-tests.mjs --ci <binary>     # CI: launch app, test, kill
//
// Requires result-mcp/: nix build .#test-framework -o result-mcp

import { resolve } from "node:path";

const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

test("package_manager_ui: loads and shows title", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Package Manager"]); },
    { timeout: 15000, interval: 500, description: "Package Manager UI to load" }
  );
});

test("package_manager_ui: shows subtitle", async (app) => {
  await app.expectTexts(["Manage plugins and packages"]);
});

test("package_manager_ui: reload button visible", async (app) => {
  await app.expectTexts(["Reload"]);
});

test("package_manager_ui: install button visible", async (app) => {
  await app.expectTexts(["Install"]);
});

test("package_manager_ui: shows release selector", async (app) => {
  await app.expectTexts(["Release:"]);
});

test("package_manager_ui: shows table headers", async (app) => {
  await app.expectTexts(["Package", "Type", "Description", "Status"]);
});

test("package_manager_ui: shows default details text", async (app) => {
  await app.expectTexts(["Select a package to view its details."]);
});

run();
