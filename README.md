# Monero Merchant Logos Module - Experimental: For development testing only!

Point of sale proof of concept with a native C++ `monero_merchant_backend `, a Basecamp invoice dashboard and an Android app. 

The `monero_merchant_backend ` uses the existing `monero_wallet_backend` through Logos SDK clients and IPC; the wallet application owns the open wallet and node connection.

The `monero_merchant_backend ` manages invoice persistence, payment tracking, terminal pairing and request authorization. Wallet spend keys stay off Android.

This is a development integration, limited to XMR invoices on Stagenet; fiat conversion, refunds, inventory and invoice cancellation/deletion are not implemented yet.

## Package Manager repository

Add repository URL via Logos Basecamp:

```text
https://raw.githubusercontent.com/Monero-Merchant/logos-monero-merchant-module/refs/heads/main/logos-repo.json
```

Packages currently support macOS Apple Silicon (`darwin-arm64`) only. 

Install `monero_merchant_ui`. Its declared dependencies resolve the merchant core, Delivery, and the required `libp2p_module`.

If an older incompatible discovery build is already installed, reinstall the compatible package from this catalog as described below.

## Use

1. Open a connected, synchronized Stagenet wallet in Basecamp's Monero wallet app.
2. In Monero Merchant, choose **1**, **5** or **10 confirmations** (default **5**).
3. Choose **Generate pairing QR**, scan the invitation on Android and approve in Basecamp. Invitations expire after five minutes and can be used once.
4. Enter an XMR amount on Android and charge. The core assigns a receiving
   subaddress in wallet account 0 and returns the invoice payment QR.
5. Track payment on Android or select the invoice in Basecamp.

## Invoice behavior and storage

| Status | Meaning |
| --- | --- |
| `pending` | No currently reported payment |
| `partial` | Received amount is below the invoice total |
| `accepted` | Full amount detected, awaiting sufficient confirmations |
| `paid` | Full amount covered by receipts meeting the invoice's saved confirmation threshold |

Amounts use exact atomic units (up to 12 XMR decimal places). Partial payments
reduce the QR's requested amount; the QR disappears once fully funded.
Additional payments accumulate and overpayments are shown. Payment status is
separate from wallet spendability.

Wallet events and a ten-second refresh reconcile receipts. Offline, syncing or
failed reads retain saved invoices with a stale indication. Missing receipts or
reduced confirmations can regress status and require review. **Mark reviewed**
acknowledges the flag without marking an invoice paid.

The core stores invoices in `merchant-v4.sqlite3` and pairing state in
`pos-v1/pos.sqlite3`, under its SDK-provided instance persistence directory.
Only the current wallet's invoices are displayed; other wallets' records remain
saved. Basecamp and `logosctl` use separate sessions and ledgers.

## Repository layout

- `src/invoices/`: invoice rules, SQLite storage and payment monitoring.
- `src/pos/`: authenticated encryption, terminal authorization and Delivery service.
- `src/monero_merchant_impl.*`: merchant SDK API and wallet integration.
- `ui/`: Basecamp dashboard and SDK bridge.
- `pos/`: Android checkout, JNI transport and native dependency builds.
- `interfaces/`: wallet backend contract.
- `scripts/`: desktop package preparation.
