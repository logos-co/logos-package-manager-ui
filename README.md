# logos-package-manager-ui

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
