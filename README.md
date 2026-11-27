# logos-package-manager-ui

A Qt/QML UI plugin that browses **multi-repository** module catalogs and lets users install, upgrade, downgrade, sidegrade, and uninstall packages. The catalog comes from the [`package_downloader`](https://github.com/logos-co/logos-package-downloader-module) module (merged across every configured repository); the UI also manages the repository list (add / remove / enable / disable). `package_downloader` does nothing until a consumer starts it, so the view calls its `start()` once both modules are connected, before its first catalog load, and again before the next if that start didn't succeed. A read that fails is not an empty catalog: a first load that failed says what it couldn't read, with Try again, and a reload that failed keeps the list it had and says so.

> 📖 Catalog formats (`logos-repo.json` / `index.json`):
> [logos-modules-release-tool/docs/catalog-format.md](https://github.com/logos-co/logos-modules-release-tool/blob/main/docs/catalog-format.md).

## Architecture

Each catalog row's primary action is resolved against its **currently
selected version** (Install / Upgrade / Downgrade / Reinstall / Retry /
Installed / Not available) and pinned to that row's **source repository**,
so a package name published by two repos can't cross-wire.

### Source vs origin

A catalog can draw packages from other catalogs (`includesUrl` in its
`logos-repo.json`). Such a row is listed — and sectioned — under the
repository the **user configured**, because that is the one they added and
grouping on anything else would render an aggregate catalog as no section at
all. So `repositoryName` is the shelf a package sits on, not necessarily who
put it there.

`originRepositoryName` is who put it there, carried per row **and per
version**: after a merge one package's versions can come from several
catalogs, so it follows the version picker. The Details panel is the surface
that says so — `Repository: My Distro (drawn from Team B)` — and it is the
only one, since the table groups by the configured repository. For a catalog
that draws from nobody the two are equal by definition, and so they are for
any row from a downloader predating the field.

### Built on basecamp-installer-common

The rows, filters, catalog loading and install pipeline are
[basecamp-installer-common](https://github.com/logos-co/basecamp-installer-common)'s,
shared with App Manager and compiled in. `src/Packages.*` is the page on it
(what the list shows, the checked rows' plan, the actions), with no module
calls of its own; `src/ModuleCalls.*` reaches package_downloader and
package_manager through the backend's typed wrappers, and
`src/PackageManagerBackend.*` mirrors `Packages` into the `.rep` and forwards
the modules' events. `src/qml/Installer` is a copy of the library's
JavaScript, the words both apps share: change them there, then run
`scripts/sync-installer-common.sh`.

### Basecamp confirms every change

PMU changes nothing on its own. Install, upgrade, downgrade, reinstall and
uninstall each raise `basecamp.packages.confirm_install` / `confirm_upgrade` /
`confirm_uninstall`, and Basecamp shows the one dialog: what the change brings
or removes, who signed it, and which running apps it stops. One change runs
at a time, in the order asked. An install or upgrade asked for meanwhile is
put to Basecamp at once and says Queued until its turn; Basecamp asks again
first if what it was planned over changed. A file or an uninstall is asked
about at its turn. A cancel is silent. Rows follow what
lands on disk through package_manager's `corePluginFileInstalled` /
`uiPluginFileInstalled` / `corePluginUninstalled` / `uiPluginUninstalled`
events, read debounced. How each request ended reaches QML as
`installationProgressUpdated`: `Completed`, or `ProgressFailed` with why
(Basecamp refused, or the change failed).

### Installing what the host approved

The library's `Installer` does exactly what Basecamp's answer approves:

- `plan`: the releases the host showed the user and judged, each pinned to
  its version, root hash, repository and signer. They are downloaded pinned,
  leaving out what is installed already as approved, and nothing installs
  unless all of them came, nothing else did, and each file is signed as
  approved: the download picks among a release's catalogue entries by root
  hash, which covers neither the manifest nor its signature.
- `file`: a picked `.lgx` is copied into a directory only this user can
  write, and that copy is what the host checks and what is installed. The
  answer carries the copy's SHA-256, root hash and signer as the host read
  them; they are checked again right before installing. A file approval
  installs that file or nothing, never a catalogue release in its place.
- `names`, for an uninstall: what may be removed, and all that is.

An upgrade fetches and checks its replacement before it removes the
installed copy; an embedded copy stays, and the replacement goes in the user
directory, where package scans select it. An answer that names neither a plan
nor a file installs nothing.

> Note: the bulk multi-select "Run Actions" surface is currently hidden
> (`selectionMode: None`) — per-row actions are the supported path. Its
> backend slots are stubs: bulk work needs one host confirmation for the
> whole batch first (see `src/package_manager_ui.rep`).

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

### Unit tests

```bash
nix build .#checks.<system>.unit-tests -L              # e.g. aarch64-darwin
nix build .#checks.<system>.shared-installer-words -L
nix build .#checks.<system>.theme-literals -L
```

`tests/cpp` runs `src/Packages` on the library with fake package modules and
a fake Basecamp, and checks the sources make no raw module calls.
`shared-installer-words` checks `src/qml/Installer` is the pinned library's
JavaScript and that no view writes out a phrase it says (exceptions, with
reasons, in `tests/shared-words-allow.txt`). `theme-literals` checks the
views take colours and sizes from the design system's `Theme` (literals no
token fits, with reasons, in `tests/theme-literals-allow.txt`).

### View tests (QML)

```bash
nix run .#qml-tests
LOGOS_DESIGN_SYSTEM=../logos-design-system nix run .#qml-tests   # a local checkout
```

`tests/qml` runs the views headless, from a phone's width to a desktop's, on a
stand-in for the backend (`tests/qml/stubs`): nothing reaches past the sides,
and the sidebar, toolbar, table and details adapt as the design system's
layouts measure. They run on the design system Basecamp's master pins, as the
views do in it (`LOGOS_BASECAMP` names another Basecamp).

### UI integration tests

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
