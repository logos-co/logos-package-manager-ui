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

      externalLibInputs = {
        lgx = {
          input = inputs.logos-package;
          packages.default = "lib";
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
        mkdir -p vendor
        cp -r ${externalLibs.lgx}/include/logos vendor/
        cp -r ${externalLibs.lgx}/include/semver vendor/
        chmod -R u+w vendor
      '';
    };
}
