{
  description = "Package Manager UI plugin for managing plugins and packages";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";

    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    package_manager.url = "github:logos-co/logos-package-manager-module";
    package_downloader.url = "github:logos-co/logos-package-downloader-module";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
