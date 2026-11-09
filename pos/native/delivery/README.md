# Native Delivery build

`flake.lock` pins the same library revision as `delivery_module`. Android
builds omit RLN, use the upstream Edge messaging API and retain Delivery's
peer/service discovery. The configured network is the non-RLN `logos.dev` preset.
The original app-specific direct TCP stream protocol and libp2p JNI library are
not linked or packaged. Delivery may use TCP internally as its network transport.

The separate Android SDK nixpkgs pin reuses the pinned NDK toolchain.
Both shared libraries must pass `pos/scripts/check-apk-alignment.py` after packaging.

`address-mapper-snapshot.patch` fixes mutation of the live address-mapper sequence
across an await in the pinned nim-libp2p dependency. This otherwise raises an
unhandled assertion on Android during NAT/address updates. The mobile Edge client
also uses `nat=none`; its network requests are outbound to Delivery service peers.
