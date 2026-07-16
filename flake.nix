{
  description = "Package Manager UI plugin for managing plugins and packages";

  inputs = {
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

      # `headers`, NOT `lib` — deliberately.
      #
      # The module builder copies every *.so/*.dylib an external library ships
      # into the plugin's output lib/, and ui-host then scans that directory and
      # tries to load each file as a Qt plugin. Pointing at logos-package's
      # `lib` output put liblgx.dylib there, ui-host failed to load it as a
      # plugin, and the whole UI never rendered. The `headers` output ships no
      # library at all, so there is nothing to copy.
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
