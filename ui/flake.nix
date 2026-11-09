{
  description = "Monero Merchant Basecamp invoice dashboard";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.2";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      ui = logos-module-builder.lib.mkLogosQmlModule {
        src = ./.;
        configFile = ./metadata.json;
        # Generate the typed client from the actual backend header in this repo.
        # Runtime modules are installed separately in Basecamp.
        flakeInputs = inputs // { merchant_source = ../.; };
      };
    in ui;
}
