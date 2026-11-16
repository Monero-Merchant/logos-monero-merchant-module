# Dependency contracts

## Wallet backend

`monero_wallet_backend.lidl` is a deliberately small consumer contract for the
pinned wallet backend. It was checked against both
`logosctl module show monero_wallet_backend` and the Rust trait in
[the pinned upstream source](https://github.com/logos-co/logos-monero-wallet-backend/blob/a17d0df6e7c89bb5a82ae196e7eb2f02599ae29a/rust-lib/src/glue.rs).

The pinned package has no LIDL asset for the module builder. `dependency_overrides` points the builder at this file;
the official SDK generator emits the typed C++ client during the build. No
wallet plugin, Rust engine, or client implementation is copied into this
project. The backend remains a required runtime dependency pinned in `metadata.json`.

The older runtime reports string types as `QString` and account indices as
`int`; the SDK contract uses `tstr` and `int` (C++ `std::string` and `int64_t`).
Account 0 fits both integer representations. The consumer contract also declares
the three `tstr` payload events `balance_changed`, `sync_progress`, and
`wallet_state_changed`, used by the incoming-payment monitor. Native Logos IPC
handles the runtime type conversion.

Before changing the backend dependency, check its installed method signatures
and JSON response formats, then update the dependency pin and contract together. When targeting a release that publishes LIDL, prefer its
pinned flake's `packages.<system>.lidl` output over this compatibility subset.

## Discovery runtime dependency

`libp2p_module.lidl` is the contract extracted from the official Logos
`libp2p_module` 1.1.0 package. The merchant declares this required dependency so
Basecamp loads discovery before the POS service. Delivery makes the discovery
calls; the merchant does not expose its general libp2p API to terminals.
Install the official Logos package instead of a Merchant-hosted copy.
