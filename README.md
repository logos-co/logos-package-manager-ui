# logos-package-manager-ui

A Qt/QML UI plugin that shows installed packages, their status, and available updates. Users can install, upgrade, downgrade, sidegrade, and uninstall packages through this UI.

## Architecture

PackageManagerBackend is a **stateless view** for uninstall/upgrade operations. It does not hold cascade state or drive confirmation dialogs — those concerns live in Basecamp's PluginManager, which owns the single cascade-confirmation dialog and orchestrates the two-phase ack protocol with the `package_manager` module.

Entry-point flow:
1. User clicks Uninstall/Upgrade in the PMU tab
2. PMU calls `package_manager.requestUninstallAsync` / `requestUpgradeAsync`
3. `package_manager` module emits `beforeUninstall` / `beforeUpgrade`
4. Basecamp's PluginManager acks, shows cascade dialog, confirms/cancels
5. PMU refreshes its package list via existing `uninstallFinished` / install events

PMU subscribes to `uninstallCancelled` / `upgradeCancelled` events for error toast display (filtered: user-initiated cancels are silent; system-originated ones show a toast via `errorOccurred`).

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
