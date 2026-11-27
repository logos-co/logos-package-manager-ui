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

    # Headers only: <logos/semver.hpp>, the version order the library's row
    # rules use. Nothing here links liblgx, so this drags in no
    # ICU/libsodium/zlib.
    logos-package.url = "github:logos-co/logos-package";

    # Compiled in: the rows, filters, catalog client and install pipeline both
    # installer apps share. src/qml/Installer is a copy of its JavaScript; the
    # shared-installer-words check keeps it this revision.
    basecamp-installer-common = {
      url = "github:logos-co/basecamp-installer-common";
      inputs.logos-nix.follows = "logos-module-builder/logos-nix";
      inputs.logos-package.follows = "logos-package";
    };

    # contracts/intents.lidl, for the backend's modules().bind_intents(): the
    # file only, so it builds nothing of intents_module.
    logos-intents-module.url = "github:logos-co/logos-intents-module";
    logos-intents-module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, logos-package, basecamp-installer-common, ... }:
    let
      shared = {
        src = ./.;
        configFile = ./metadata.json;
        flakeInputs = inputs;

        # `headers`, NOT `lib` — because nothing here links lgx.
        #
        # This plugin's only use of logos-package is the header-only
        # `logos/semver.hpp` (the library's row rules); CMakeLists declares no
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
            input = logos-package;
            packages.default = "headers";
          };
        };

        # The library and the semver headers, staged where CMakeLists.txt looks.
        #
        # The builder's normal external-lib staging flattens `include/*.h` into a
        # single `lib/` directory, which would both drop our `.hpp` files and
        # collapse `logos/semver.hpp` and `semver/semver.hpp` onto the same name.
        # Copy the directories across intact instead. preConfigure gets the
        # per-system resolved derivation.
        preConfigure = { externalLibs }: ''
          # Clear any previously staged tree first, so an incremental/local build
          # after an input changes can't leave a stale one behind. (Nix sandbox
          # builds start clean, but `nix develop` reuses the source dir.)
          rm -rf vendor
          mkdir -p vendor/basecamp-installer-common
          cp -r ${externalLibs.lgx}/include/logos vendor/
          cp -r ${externalLibs.lgx}/include/semver vendor/
          cp -r ${basecamp-installer-common}/include ${basecamp-installer-common}/src \
                ${basecamp-installer-common}/cmake vendor/basecamp-installer-common/
          chmod -R u+w vendor
        '';
      };

      module = logos-module-builder.lib.mkLogosQmlModule shared;

      lib = logos-module-builder.inputs.nixpkgs.lib;
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      pkgsFor = system: import logos-module-builder.inputs.nixpkgs { inherit system; };

      checksFor = system:
        let pkgs = pkgsFor system;
        in {
          # The page's logic on the library, with fake package modules and a
          # fake Basecamp (tests/cpp). mkLogosQmlModule only picks up
          # tests/*.mjs (`nix run .#ui-tests`), which runs without a catalogue.
          unit-tests = pkgs.stdenv.mkDerivation {
            name = "logos-package-manager-ui-unit-tests";
            src = ./.;
            cmakeDir = "../tests";
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs = [ pkgs.qt6.qtbase ];
            dontWrapQtApps = true;
            cmakeFlags = [
              "-DBASECAMP_INSTALLER_COMMON_DIR=${basecamp-installer-common}"
              "-DLOGOS_PACKAGE_INCLUDE_DIR=${logos-package.packages.${system}.headers}/include"
            ];
            QT_QPA_PLATFORM = "offscreen";
            # The sandbox has no network to ask about.
            BASECAMP_FORCE_OFFLINE = "0";
            doCheck = true;
            checkPhase = "ctest --output-on-failure --verbose";
            installPhase = "touch $out";
          };

          # src/qml/Installer is the pinned library's JavaScript, and no view
          # writes out a phrase it says.
          shared-installer-words = pkgs.runCommand "logos-package-manager-ui-shared-installer-words" {
            nativeBuildInputs = [ pkgs.python3 ];
          } ''
            cd ${./.}
            diff -r ${basecamp-installer-common}/qml/Installer src/qml/Installer \
              || { echo "src/qml/Installer is not the pinned basecamp-installer-common: run scripts/sync-installer-common.sh"; exit 1; }
            python3 ${basecamp-installer-common}/tools/shared-words-check \
              ${basecamp-installer-common}/qml/Installer src/qml \
              --skip src/qml/Installer --allow tests/shared-words-allow.txt
            touch $out
          '';
        };

      # The views' QML tests (tests/qml), from a phone's width to a desktop's:
      # qmltestrunner, offscreen, on a stand-in for the backend
      # (tests/qml/stubs), so no host or backend is needed. They run on the
      # design system Basecamp's master pins, as the views do in it, read from
      # its lock at run time: an app, like `nix run .#ui-tests`, as the Nix
      # sandbox cannot fetch Basecamp. LOGOS_BASECAMP names another Basecamp,
      # LOGOS_DESIGN_SYSTEM a design-system checkout to run on instead;
      # arguments pass to qmltestrunner.
      qmlTests = system:
        let
          pkgs = pkgsFor system;
          qml = pkgs.qt6.qtdeclarative;
          plugins = pkgs.qt6.qtbase.qtPluginPrefix;
          pinOf = pkgs.writeText "design-system-pin.jq" ''
            .locks.nodes as $n | $n[$n.root.inputs["logos-design-system"]].locked
            | "\(.type):\(.owner)/\(.repo)/\(.rev)"
          '';
          run = pkgs.writeShellApplication {
            name = "run-qml-tests";
            runtimeInputs = [ pkgs.jq ];
            text = ''
              ds="''${LOGOS_DESIGN_SYSTEM:-}"
              if [ -z "$ds" ]; then
                ref="''${LOGOS_BASECAMP:-github:logos-co/logos-basecamp}"
                pin=$(nix flake metadata "$ref" --json | jq -r -f ${pinOf})
                echo "run-qml-tests: on the design system $ref pins, $pin" >&2
                ds=$(nix flake prefetch "$pin" --json | jq -r .storePath)
              fi
              export QT_QPA_PLATFORM=offscreen
              export QT_QUICK_CONTROLS_STYLE="''${QT_QUICK_CONTROLS_STYLE:-Basic}"
              export QML_XHR_ALLOW_FILE_READ=1
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${plugins}:${pkgs.qt6.qtsvg}/${plugins}"
              exec ${qml}/bin/qmltestrunner \
                -input ${./.}/tests/qml \
                -import ${./.}/src/qml \
                -import ${./.}/tests/qml/stubs \
                -import "$ds/src/qml" \
                -import ${qml}/${pkgs.qt6.qtbase.qtQmlPrefix} "$@"
            '';
          };
        in { type = "app"; program = "${run}/bin/run-qml-tests"; };
    in
    module // {
      checks = lib.genAttrs systems
        (system: ((module.checks or { }).${system} or { }) // checksFor system);
      apps = builtins.mapAttrs
        (system: apps: apps
          // (if system == "x86_64-windows" then {} else { qml-tests = qmlTests system; }))
        module.apps;
    };
}
