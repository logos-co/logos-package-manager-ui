# logos-package-manager-ui

A Qt/QML UI plugin that browses **multi-repository** module catalogs and lets users install, upgrade, downgrade, sidegrade, and uninstall packages. The catalog comes from the [`package_downloader`](https://github.com/logos-co/logos-package-downloader-module) module (merged across every configured repository); the UI also manages the repository list (add / remove / enable / disable).

> 📖 Catalog formats (`logos-repo.json` / `index.json`):
> [logos-modules-release-tool/docs/catalog-format.md](https://github.com/logos-co/logos-modules-release-tool/blob/main/docs/catalog-format.md).

## Architecture

Each catalog row's primary action is resolved against its **currently
selected version** (Install / Upgrade / Downgrade / Reinstall / Retry /
Installed / Not available) and pinned to that row's **source repository**,
so a package name published by two repos can't cross-wire.

### Dependency-aware per-row actions (owned by PMU)

When the user triggers an Install / Upgrade / Downgrade / Reinstall, PMU
previews the dependency impact **before** anything downloads:

1. `package_downloader.resolveDependencies(deps, installed)` resolves what
   the action would change, omitting transitive deps already satisfied
   on-disk.
2. **No transitive changes** → proceed straight to the download/install.
3. **Transitive changes** → the backend stashes the pending request
   (keyed by an opaque `requestKey` = `repositoryUrl` + name, unique
   across repos) and emits `installDepsConfirmationRequested`; QML shows
   the `InstallDepsConfirm` dialog offering **Install with dependencies /
   Install just this / Cancel**. The chosen path routes back through
   `confirmInstallWith{,out}Deps` / `cancelInstallConfirm` by that key.

So, unlike the uninstall/upgrade *cascade* dialog, PMU **does** own this
dep-confirm preview state.

### Uninstall / upgrade cascade (owned by Basecamp)

For the destructive cascade confirmation, PackageManagerBackend is a
stateless view — Basecamp's PluginManager owns the single
cascade-confirmation dialog and the two-phase ack protocol:

1. User clicks Uninstall / Upgrade in the PMU tab
2. PMU calls `package_manager.requestUninstallAsync` / `requestUpgradeAsync`
3. `package_manager` emits `beforeUninstall` / `beforeUpgrade`
4. Basecamp's PluginManager acks, shows the cascade dialog, confirms/cancels
5. On completion, `package_manager` emits `corePluginFileInstalled` / `uiPluginFileInstalled` / `corePluginUninstalled` / `uiPluginUninstalled`, which PMU consumes (debounced) to refresh the catalog rows

PMU subscribes to `uninstallCancelled` / `upgradeCancelled` events for error toast display. User-initiated cancels are silent; system-originated cancellations (e.g. the module's ack-timeout when no listener takes over the gated flow) are surfaced via the dedicated `cancellationOccurred(name, message)` signal, which QML renders as a plain toast. The install-progress channel (`installationProgressUpdated`) is reserved for install progress and install failures; routing cancellations through it would render them with a misleading "Failed to install" prefix.

> Note: the bulk multi-select "Run Actions" surface is currently hidden
> (`selectionMode: None`) — per-row actions are the supported path. The
> backend plumbing for it remains in place behind that flag.

## How to Build

### Using Nix (Recommended)

#### Build Complete UI Plugin

```bash
# Build everything (default)
nix build

# Or explicitly
nix build '.#default'
```

The result will include:
- `/lib/package_manager_ui.dylib` (or `.so` on Linux) - The Package Manager UI plugin

#### Build Individual Components

```bash
# Build only the library (plugin)
nix build '.#lib'

# Build the standalone Qt application
nix build '.#app'
```

#### Development Shell

```bash
# Enter development shell with all dependencies
nix develop
```

**Note:** In zsh, you need to quote the target (e.g., `'.#default'`) to prevent glob expansion.

If you don't have flakes enabled globally, add experimental flags:

```bash
nix build --extra-experimental-features 'nix-command flakes'
```

The compiled artifacts can be found at `result/`

#### Running the Standalone App

After building the app with `nix build '.#app'`, you can run it:

```bash
# Run the standalone Qt application
# you need sudo if you install packages due to the permissions needed on `./result/` which is a nix mounted folder
sudo ./result/bin/logos-package-manager-ui-app
```

note: installed packages will go to `./result/modules`

#### Nix Organization

The nix build system is organized into modular files in the `/nix` directory:
- `nix/default.nix` - Common configuration (dependencies, flags, metadata)
- `nix/lib.nix` - UI plugin compilation
- `nix/app.nix` - Standalone Qt application compilation

## Output Structure

When built with Nix:

**Library build (`nix build '.#lib'`):**
```
result/
└── lib/
    └── package_manager_ui.dylib    # Logos Package Manager UI plugin
```

**App build (`nix build '.#app'`):**
```
result/
├── bin/
│   ├── logos-package-manager-ui-app    # Standalone Qt application
│   ├── logos_host                       # Logos host executable (for plugins)
│   └── logoscore                        # Logos core executable
├── lib/
│   ├── liblogos_core.dylib              # Logos core library
│   └── liblogos_sdk.dylib               # Logos SDK library
├── modules/
│   ├── capability_module_plugin.dylib
│   └── package_manager_plugin.dylib
└── package_manager_ui.dylib             # Qt plugin (loaded by app)
```

## Testing

UI integration tests use [logos-qt-mcp](https://github.com/logos-co/logos-qt-mcp) to drive the plugin UI inside [logos-standalone-app](https://github.com/logos-co/logos-standalone-app) (headless).

### Hermetic CI test (one command)

```bash
nix build .#integration-test -L
```

This builds everything (including logos-standalone-app and the QML inspector), launches the plugin in headless mode, and runs the UI tests automatically. No extra inputs needed — the test infrastructure is provided by logos-module-builder via logos-standalone-app.

### Interactive testing

```bash
# 1. Build the plugin
nix build

# 2. Build the test framework (one-time)
nix build .#test-framework -o result-mcp

# 3. Run the plugin in logos-standalone-app (inspector starts on :3768)
nix run

# 4. Run the tests against the running app
node tests/ui-tests.mjs
```


## Requirements

### Build Tools
- CMake (3.16 or later)
- Ninja build system
- pkg-config

### Dependencies
- Qt6 (qtbase)
- Qt6 Widgets (included in qtbase)
- Qt6 Remote Objects (qtremoteobjects)
- Qt6 Declarative (qtdeclarative)
- logos-liblogos
- logos-cpp-sdk (for header generation)
- logos-package-manager-module
- logos-capability-module
- zstd
- krb5
- abseil-cpp
