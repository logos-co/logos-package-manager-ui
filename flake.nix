{
  description = "Logos Package Manager UI - Online catalog and download/install interface";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
    logos-package-manager-module.url = "github:logos-co/logos-package-manager-module";
    logos-package-downloader-module.url = "github:logos-co/logos-package-downloader-module";
    logos-capability-module.url = "github:logos-co/logos-capability-module";
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-liblogos, logos-package-manager-module, logos-package-downloader-module, logos-capability-module }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
        logosPackageManagerModule = logos-package-manager-module.packages.${system}.default;
        logosPackageDownloaderModule = logos-package-downloader-module.packages.${system}.default;
        logosCapabilityModule = logos-capability-module.packages.${system}.default;
      });
    in
    {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosPackageManagerModule, logosPackageDownloaderModule, logosCapabilityModule }:
        let
          # Common configuration
          common = import ./nix/default.nix {
            inherit pkgs logosSdk logosLiblogos;
          };
          src = ./.;

          # Library package (development build)
          lib = import ./nix/lib.nix {
            inherit pkgs common src logosPackageManagerModule logosPackageDownloaderModule logosSdk;
          };

          # Library package (distributed build for DMG/AppImage)
          libDistributed = import ./nix/lib.nix {
            inherit pkgs common src logosPackageManagerModule logosPackageDownloaderModule logosSdk;
            distributed = true;
          };

          # App package
          app = import ./nix/app.nix {
            inherit pkgs common src logosLiblogos logosSdk logosPackageManagerModule logosPackageDownloaderModule logosCapabilityModule;
            logosPackageManagerUI = lib;
          };
        in
        {
          # Individual outputs
          logos-package-manager-ui-lib = lib;
          app = app;
          lib = lib;

          # Distributed build (for DMG/AppImage)
          distributed = libDistributed;

          # Default package
          default = lib;
        }
      );

      devShells = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosPackageManagerModule, logosPackageDownloaderModule, logosCapabilityModule }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
            pkgs.qt6.qtdeclarative
            pkgs.zstd
            pkgs.krb5
            pkgs.abseil-cpp
          ];

          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
            echo "Logos Package Manager UI development environment"
            echo "LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "LOGOS_LIBLOGOS_ROOT: $LOGOS_LIBLOGOS_ROOT"
          '';
        };
      });
    };
}
