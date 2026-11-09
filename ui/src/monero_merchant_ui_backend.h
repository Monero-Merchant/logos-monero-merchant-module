#pragma once

#include "rep_monero_merchant_ui_source.h"
#include "logos_ui_plugin_context.h"
#include <QTimer>

class MoneroMerchantUiBackend : public MoneroMerchantUiSimpleSource,
                                public LogosUiPluginContext
{
public:
    QString getWalletStatus() override;
    QString getInvoices() override;
    QString setConfirmationPolicy(int confirmations) override;
    QString createInvoice(QString requestId, QString title, QString amountXmr) override;
    QString recoverInvoice(QString invoiceId) override;
    QString refreshInvoices() override;
    QString acknowledgeInvoiceReview(QString invoiceId) override;
    QString paymentQr(QString paymentUri) override;
    QString getPosPairing() override;
    QString createPosInvitation() override;
    QString approvePosDevice(QString peerId) override;
    QString revokePosDevice(QString peerId) override;
    QString pairingQr(QString invitation) override;

protected:
    void onContextReady() override;

private:
    void pullInvoices();
    QTimer invoiceRecovery_;
};
