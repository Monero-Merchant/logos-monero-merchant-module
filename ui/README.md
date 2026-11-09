# Monero Merchant — Basecamp invoice dashboard

A single-page QML app with a process-isolated C++ bridge to `monero_merchant_backend`.
The page contains wallet connection status, confirmation policy, POS pairing, saved invoices,
and payment details. Diagnostic test controls, wallet-wide payment browsing and
standalone address creation have been removed from the UI and its bridge.
The merchant core retains its CLI APIs and payment monitor.

## Build and install

Run from the repository root, adding `/nix/var/nix/profiles/default/bin` to PATH
if needed. Include new files in Git before building Git-backed Nix flakes.

```sh
nix build '.#lgx-portable' -L
nix build './ui#lgx-portable' --out-link ui/result -L
```

Install the merchant core and UI packages, plus Delivery and its discovery dependency, through
Basecamp's Package Manager. Restart Basecamp after updating native libraries.
The existing wallet stack must provide the compatible `monero_wallet_backend` dependency declared in `metadata.json`.
Basecamp and `logosctl` have separate wallet sessions and persistent storage.

## Pair an Android POS

The **Pair POS** card appears below the wallet/confirmation cards. Pairing and
Delivery now run inside the C++ merchant core, with no Python service or local
socket adapter. The UI calls the merchant's generated SDK API. Closing this UI
view does not stop a loaded merchant core's POS service.

The card reports Delivery readiness. Choose **Generate pairing QR**, scan or paste
the invitation on Android, compare the full terminal fingerprints, and approve.
Invitations expire after five minutes and can be used once. **Revoke** blocks the
next request and cached replies; a revoked terminal must generate a fresh key.
The card refreshes terminal state every three seconds. Invoice tracking remains
in this dashboard; new orders are entered on the Android POS.

This development build uses fresh identity/pairing and invoice storage. Existing
files and wallets are not deleted or imported. Forget the old pairing on Android
and pair again. See [the POS instructions](../pos/README.md).

## One-page workflow

1. Open a Stagenet wallet in the existing Monero wallet app.
2. Check **Wallet connection**. Backend reachability, open wallet, node connection
   and synchronization are reported separately. A connected UI bridge alone does
   not prove that the wallet backend is available. Status is checked automatically
   every 15 seconds and with **Check connection**; errors clear previous success.
3. Choose **1**, **5** or **10 confirmations** in the small **Confirmation Policy**
   card to the right of Wallet Connection (stacked below it on narrow windows).
   This persistent default applies only to new invoices; existing invoices keep
   their original policy. The dropdown shows **1 (~2 min)**, **5 (~10 min)** and **10 (~20 min)**.
   Times are approximate; Five remains the default.
4. Create an order on the paired Android POS. The backend saves a unique
   receiving subaddress association.
5. Select a saved invoice to see its QR, address, received/confirmed/remaining
   amounts, transaction hashes and confirmation counts.
6. Updates arrive automatically. **Refresh** requests wallet reconciliation and
   checks the connection again; it creates no address or transaction.

POS invoice creation requires that the backend reports a connected, synchronized
Stagenet wallet and invoice storage is ready. The app does not open, restore,
close or switch wallets itself.

## Invoice behavior

- **Pending:** no currently reported payment.
- **Partially paid:** received amount is below the invoice total.
- **Accepted:** full amount detected, awaiting sufficient confirmations.
- **Paid:** sufficient receipts have individually met the confirmation policy saved with that invoice.

All amounts retain 12-decimal XMR precision. QR codes encode the remaining amount,
include a four-module quiet zone, and are generated locally with libqrencode.
The QR disappears once the invoice is fully funded. Addresses and hashes remain
selectable; **Copy address** uses the local clipboard.

The POS reuses its stable order identity when retrying invoice creation.
**Recover receiving address** reconciles an interrupted allocation without
allocating another address.

Only the open wallet's invoices appear. Closing or changing wallets clears the
previous selection and QR; returning restores that wallet's saved records.
Offline, syncing or failed reconciliation retains records with a stale notice.
Missing receipts and confirmation regressions can change invoice status and flag
it for review. **Mark reviewed** acknowledges the flag without marking it paid.
The backend keeps its SQLite ledger and first accepted/paid timestamps.
