# Android POS — Logos Delivery proof of concept

Kotlin/Jetpack Compose checkout with `liblogosdelivery` embedded through JNI.
The C++ merchant backend communicates over the Logos Delivery network using
`delivery_module` in the same Basecamp context as Monero Merchant.
There is no direct POS-to-merchant TCP transport or CLI wallet fallback.
Delivery itself selects its underlying network transports.

## Architecture

```text
Compose checkout → MerchantConnection → authenticated HPKE messages
        → JNI → liblogosdelivery → Logos Delivery network
        → Basecamp delivery_module → C++ Monero Merchant POS service
        → existing invoice engine → selected wallet
```

The native library and module are pinned to compatible revisions:

- Delivery module: `586e1a383ab2286be0c3a5f1d0f864a8fd37f5b8`.
- Delivery library: `6b1f3a09e2c59442bb592659137c660d64a23baa`.
- Development network: `logos.dev`, Edge mode, Mix disabled for initial checkout.

This build disables native RLN and deliberately accepts only `logos.dev`, whose
pinned preset does not require RLN. Supporting an RLN-enabled network requires
integrating its membership/proof provider on Android and testing its quotas.
No mainnet, Mix anonymity, or guaranteed background operation is claimed.

## Build Android

Requires Nix, Android Studio with its bundled JDK, and the Android SDK/NDK selected in the Gradle configuration.
Set `sdk.dir` in the ignored `local.properties` file for your machine.

```sh
cd pos
./scripts/prepare-native.sh android
./gradlew assembleDebug
python3 scripts/check-apk-alignment.py app/build/outputs/apk/debug/app-debug.apk
```

The preparation script builds arm64-v8a and x86_64 artifacts from
`native/delivery/flake.lock`. JNI and the native library link with 16 KiB page
alignment. The APK checker also verifies ELF RELRO boundaries and ZIP alignment.
The output is `app/build/outputs/apk/debug/app-debug.apk`.

## Prepare Basecamp

Build/install `delivery_module`, its compatible discovery module, and the
Monero Merchant core and UI packages. The core owns Delivery and POS
communication; the UI calls merchant SDK methods only. The wallet backend must
provide the compatible `monero_wallet_backend` declared in the package metadata.

```sh
scripts/prepare-backend.sh
```

Desktop package links are written to `artifacts/backend/`. Install their `.lgx`
packages using Basecamp's Package Manager, then fully
quit and reopen Basecamp after replacing native modules. Open a Stagenet wallet
in the wallet app. The POS service starts when the merchant core context loads
and continues without the merchant UI view. It uses the selected wallet; no
wallet name is configured or opened by the POS service.

There is no Python service, virtualenv, or adapter socket to start.
Native state is private to the merchant core's instance persistence directory:
`pos-v1/pos.sqlite3` contains its new identity, terminal authorizations, invitations,
request cache and order mappings. The fresh invoice ledger is `merchant-v4.sqlite3`.
Older data is neither imported nor deleted. Create a new wallet if desired.

**Fresh pairing is required:** forget the old merchant/terminal pairing on Android,
generate a new invitation in Basecamp, and approve the new terminal fingerprint.
Do not reuse an old QR or expect an old order to appear in the fresh ledger.

## Pair and use

1. In Basecamp's **Pair POS** card, choose **Generate pairing QR**. No shop IP or
   port is needed. Both endpoints must reach the configured Delivery network.
2. Scan the invitation on Android, or paste its JSON. The invitation expires in
   five minutes and pins the merchant X25519 public identity and network.
3. Compare the terminal identity shown on both screens, then approve in Basecamp.
4. Enter an XMR amount and charge. The merchant creates the invoice and returns
   its payment QR. Checkout polls authoritative merchant status every five seconds.
5. Accepted payments show success. **New order** resets the amount keypad.
   **Transactions** shows invoices owned by this terminal and current wallet scope.
6. **Disconnect / Reconnect** preserves pairing and the current order. Forgetting
   removes the phone's saved terminal identity and pairing; revoke it in Basecamp too.

## Authentication and request handling

Network messages use RFC 9180 HPKE AUTH mode: X25519, HKDF-SHA256, AES-128-GCM,
implemented by Bouncy Castle on Android and OpenSSL HPKE in the C++ backend.
The encrypted envelope binds sender, recipient, direction, topic, request UUID and
issue time as associated data. Messages outside a two-minute clock window are
rejected. Keep both device clocks correct. This authenticates application keys;
Delivery topic names, network peer IDs and send acknowledgements are not permission.

A request UUID correlates each reply. A durable replay record prevents duplicate
execution; the stable order UUID separately makes invoice creation idempotent.
After an uncertain outcome, reconnect and retrieve the authoritative invoice.
Revocation is checked before backend access and before reusing a cached reply.

Only Pair, Session, GetBackendStatus, CreateInvoice, GetInvoice and GetInvoices
are exposed. Invoice access is scoped to the authenticated terminal, current
Basecamp merchant ledger and wallet identity. Wallet spend keys never go to Android.
Existing invoices are not canceled by returning to **New order**.

`send` acceptance only means Delivery accepted the message operation. The POS
marks itself connected after an authenticated merchant reply, and payment succeeds
only when the merchant reports an accepted/paid invoice. Store catch-up and network
retries can duplicate or delay traffic; stale and mismatched replies are ignored.

## Validation status

Earlier native protocol/lifecycle, invoice/wallet, Kotlin, and QML checks passed.
Test source, fixtures, and generated reports were removed during repository
cleanup. These historical checks do not establish live C++ Delivery connectivity.

Verify fresh pairing against the installed core, a real wallet invoice/payment,
core restart, UI close/reopen, and Android reconnect before relying on checkout.
Also verify separate physical networks and mobile-data/Wi-Fi switching.

## Pairing crash correction

The pinned nim-libp2p address manager iterated a live mapper sequence across an
await. NAT/address updates could mutate that sequence and terminate the Android
process with `the length of the seq changed while iterating over it`. The Android
native build now applies an independent mapper snapshot. JNI callbacks also use
monotonic registry tickets and shared event state so late callbacks cannot lock a
freed connection mutex; listeners are removed before native teardown.

The mobile Edge client disables router port mapping (`nat=none`). Pairing and
orders remain in their existing encrypted storage. The C++ backend reuses a
shared Delivery node and restores its subscription when the SDK reconnects.

Before cleanup, the pairing-screen regression, reconnect/activity recreation, recoverable timeout,
and callback lifetime checks passed. The callback checks also passed under
AddressSanitizer/UndefinedBehaviorSanitizer. Test code and fixtures are no longer
included in this repository.
