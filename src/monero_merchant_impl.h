#pragma once
#include <cstdint>

#include "logos_module_context.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class IncomingMonitor;
namespace merchant
{
class InvoiceStore;
namespace pos
{
class Delivery;
}
} // namespace merchant

class MoneroMerchantImpl : public LogosModuleContext
{
public:
    ~MoneroMerchantImpl() override;

    /// Returns the wallet backend's JSON status, including network and synchronization.
    std::string getWalletStatus();

    /// Returns persisted invoices, their receipts and monitoring freshness as JSON.
    std::string getInvoices();

    /// Sets the persisted confirmation default (1, 5 or 10) for new invoices only.
    std::string setConfirmationPolicy(int64_t confirmations);

    /// Creates one XMR invoice with an idempotency key and a new account 0 subaddress.
    std::string createInvoice(const std::string& requestId, const std::string& title,
                              const std::string& amountXmr);

    /// Recovers an interrupted address assignment by label; never creates an address.
    std::string recoverInvoice(const std::string& invoiceId);

    /// Schedules wallet reconciliation for invoice status updates.
    std::string refreshInvoices();

    /// Records that an operator reviewed a receipt change; does not change payment status.
    std::string acknowledgeInvoiceReview(const std::string& invoiceId);

    /// Returns terminal records and readiness of the integrated Delivery service.
    std::string getPosPairing();
    /// Creates a single-use five-minute pairing invitation.
    std::string createPosInvitation();
    /// Approves an enrolled terminal key after operator verification.
    std::string approvePosDevice(const std::string& peerId);
    /// Revokes a terminal key; a revoked key cannot enroll again.
    std::string revokePosDevice(const std::string& peerId);

    // clang-format off
logos_events:
    /// Persisted invoice snapshot, emitted after the database transaction commits.
    void invoicesChanged(const std::string& payload);
    // clang-format on

protected:
    void onContextReady() override;
    LogosShutdown aboutToUnload() override;

private:
    std::recursive_mutex stateMutex_;
    // Never wait on this gate: a wallet IPC call can re-enter the main thread.
    std::mutex invoiceOperationMutex_;
    std::shared_ptr<merchant::pos::Delivery> pos_;
    std::string posError_;
    std::string createInvoiceForWallet(const std::string& requestId, const std::string& title,
                                       const std::string& amountXmr, const std::string& expectedWallet);
    std::shared_ptr<IncomingMonitor> incoming_;
    std::function<void()> cancelSubscriptions_;
    std::shared_ptr<merchant::InvoiceStore> invoices_;
    std::string invoiceStorageError_;
    std::string finishInvoiceAddress(const std::shared_ptr<merchant::InvoiceStore>& store,
                                     const std::string& id, bool allocate);
    void publishInvoices();
};
