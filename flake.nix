{
  description = "Monero Merchant POS - native Logos wallet IPC proof of concept";

  inputs = {
    # SDK generation needs the pinned header, not Delivery's native/RLN build graph.
    delivery_module = {
      url = "github:logos-co/logos-delivery-module/586e1a383ab2286be0c3a5f1d0f864a8fd37f5b8";
      flake = false;
    };
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.2";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      merchant = logos-module-builder.lib.mkLogosModule {
        src = ./.;
        configFile = ./metadata.json;
        flakeInputs = inputs;
      };
    in merchant;
}
