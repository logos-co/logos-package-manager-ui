{
  description = "Package Manager UI plugin for managing plugins and packages";

  inputs = {
    # Unpinned again. This carried c60d4a9 (tip of
    # feat/sdk-codegen-b4-qt-host-repoint) because the generated view-plugin
    # glue this module now relies on is emitted by
    # `logos-qt-generator --backend ui`, and neither that generator backend nor
    # the builder's `uiCodegen` hand-off had reached the builder's master.
    # logos-module-builder#203 ("take the view templates from
    # logos-view-module") merged the whole B4 stack: master (8cd62c7) has
    # lib/modulePreConfigure.nix's `uiCodegen` — byte-identical to the pinned
    # rev's — and carries ZERO rev pins of its own, so it locks logos-cpp-sdk,
    # logos-qt-sdk, logos-plugin-qt and logos-protocol at their (also merged)
    # masters. NOTE: #203 was SQUASH-merged, so
    # `git merge-base --is-ancestor c60d4a9 master` is correctly false —
    # ancestry is the wrong test here, the files are the test.
    logos-module-builder.url = "github:logos-co/logos-module-builder";

    package_manager.url = "github:logos-co/logos-package-manager-module";
    package_downloader.url = "github:logos-co/logos-package-downloader-module";

    # Header-only: supplies the shared semver implementation
    # (include/logos/semver.hpp), which RowActionResolver.h uses to decide
    # Upgrade / Downgrade / Reinstall. We take the headers only — nothing here
    # links liblgx, so this drags in no ICU/libsodium/zlib.
    logos-package.url = "github:logos-co/logos-package";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;

      # `headers`, NOT `lib` — because nothing here links lgx.
      #
      # This plugin's only use of logos-package is the header-only
      # `logos/semver.hpp` (RowActionResolver.h); CMakeLists declares no
      # EXTERNAL_LIBS and the built plugin's load commands name lgx zero times.
      # The `lib` output ships liblgx.dylib, which the module builder then
      # stages beside the plugin and into its .lgx — 0.6 MB of library nothing
      # loads. `headers` ships no library, so there is nothing to copy.
      #
      # This choice used to be load-bearing for a second, sharper reason, and
      # the note it carried was wrong about the mechanism. ui-host does NOT scan
      # a directory: it takes `--path <plugin>` and loads exactly that file.
      # What actually broke was logos-standalone-app picking the backend by
      # globbing the install dir and taking the alphabetically first library, so
      # liblgx.dylib ("l" < "p") was handed to ui-host in place of the plugin.
      # Fixed in logos-standalone-app#44, which resolves manifest.json's `main`.
      # Re-checked against that fix with `lib` and liblgx.dylib present:
      # ui-host is spawned with package_manager_ui_plugin.dylib. So `lib` is now
      # merely wasteful rather than fatal — hence still `headers`, on the
      # first reason alone.
      externalLibInputs = {
        lgx = {
          input = inputs.logos-package;
          packages.default = "headers";
        };
      };

      # Stage the shared semver headers into the tree so CMake can see them.
      #
      # The builder's normal external-lib staging flattens `include/*.h` into a
      # single `lib/` directory, which would both drop our `.hpp` files and
      # collapse `logos/semver.hpp` and `semver/semver.hpp` onto the same name.
      # Copy the directories across intact instead; CMakeLists puts `vendor/` on
      # the include path. preConfigure gets the per-system resolved derivation.
      preConfigure = { externalLibs }: ''
        # Clear any previously staged headers first, so an incremental/local
        # build after the logos-package input changes can't leave a stale tree
        # behind. (Nix sandbox builds start clean, but `nix develop` reuses the
        # source dir.)
        rm -rf vendor
        mkdir -p vendor
        cp -r ${externalLibs.lgx}/include/logos vendor/
        cp -r ${externalLibs.lgx}/include/semver vendor/
        chmod -R u+w vendor
      '';
    };
}
