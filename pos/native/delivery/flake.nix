{
  description = "Pinned Logos Delivery Android library for Merchant POS";
  inputs.delivery.url = "github:logos-messaging/logos-delivery/6b1f3a09e2c59442bb592659137c660d64a23baa";
  inputs.android-nixpkgs.url = "github:NixOS/nixpkgs/ac62194c3917d5f474c1a844b6fd6da2db95077d";
  inputs.nixpkgs.follows = "delivery/nixpkgs";
  outputs = { self, delivery, nixpkgs, android-nixpkgs }: let
    systems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
  in {
    packages = nixpkgs.lib.genAttrs systems (system: let
      pkgs = import nixpkgs { inherit system; config.android_sdk.accept_license = true; config.allowUnfree = true; };
      androidPkgs = import android-nixpkgs { inherit system; config.android_sdk.accept_license = true; config.allowUnfree = true; };
      mkAndroid = abi: triple: cpu: import ./android.nix { inherit pkgs androidPkgs delivery abi triple cpu; };
    in { default = mkAndroid "arm64-v8a" "aarch64-linux-android" "arm64";
         android-arm64 = self.packages.${system}.default;
         android-x86_64 = mkAndroid "x86_64" "x86_64-linux-android" "amd64";
    });
  };
}
