#include "monero_merchant_ui_backend.h"
#include "logos_sdk.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QMetaObject>
#include "payment_qr.h"

namespace {
QString checkedReply(const QString& reply, const logos::CallError& error)
{
    if (error.ok()) return reply;
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"ok", false}, {"code", QString::fromStdString(error.code)},
        {"error", QString::fromStdString(error.message)},
        {"origin", QString::fromStdString(error.origin)}
    }).toJson(QJsonDocument::Compact));
}
}

QString MoneroMerchantUiBackend::getWalletStatus()
{
    logos::CallError error;
    const auto reply = modules().monero_merchant.getWalletStatus(&error);
    return checkedReply(reply, error);
}

void MoneroMerchantUiBackend::onContextReady()
{
    QPointer<MoneroMerchantUiBackend> self(this);
    modules().monero_merchant.onInvoicesChanged([self](const QString& snapshot) {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, snapshot] { if (self) self->setInvoices(snapshot); }, Qt::QueuedConnection);
    });
    // Initial/reconnection snapshot: ordinary pushes update the property, and
    // this inexpensive cached read repairs a missed event after a provider loss.
    QObject::connect(&invoiceRecovery_, &QTimer::timeout, this, [this] { pullInvoices(); });
    invoiceRecovery_.start(15000);
    QTimer::singleShot(0, this, [this] { pullInvoices(); });
}

QString MoneroMerchantUiBackend::getInvoices() {
    logos::CallError error;
    const auto reply = modules().monero_merchant.getInvoices(&error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::setConfirmationPolicy(int confirmations) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.setConfirmationPolicy(confirmations, &error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::createInvoice(QString requestId, QString title, QString amountXmr) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.createInvoice(requestId, title, amountXmr, &error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::recoverInvoice(QString invoiceId) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.recoverInvoice(invoiceId, &error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::refreshInvoices() {
    logos::CallError error;
    const auto reply = modules().monero_merchant.refreshInvoices(&error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::acknowledgeInvoiceReview(QString invoiceId) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.acknowledgeInvoiceReview(invoiceId, &error);
    return checkedReply(reply, error);
}
void MoneroMerchantUiBackend::pullInvoices() {
    QPointer<MoneroMerchantUiBackend> self(this);
    modules().monero_merchant.getInvoicesAsyncResult([self](logos::AsyncResult<QString> result) {
        if (!self) return;
        const auto reply = checkedReply(result.value, result.error);
        QMetaObject::invokeMethod(self, [self, reply] { if (self) self->setInvoices(reply); }, Qt::QueuedConnection);
    });
}
QString MoneroMerchantUiBackend::paymentQr(QString paymentUri) { return encodePaymentQr(paymentUri); }
QString MoneroMerchantUiBackend::getPosPairing() {
    logos::CallError error;
    const auto reply = modules().monero_merchant.getPosPairing(&error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::createPosInvitation() {
    logos::CallError error;
    const auto reply = modules().monero_merchant.createPosInvitation(&error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::approvePosDevice(QString peerId) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.approvePosDevice(peerId, &error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::revokePosDevice(QString peerId) {
    logos::CallError error;
    const auto reply = modules().monero_merchant.revokePosDevice(peerId, &error);
    return checkedReply(reply, error);
}
QString MoneroMerchantUiBackend::pairingQr(QString invitation) { return encodePairingQr(invitation); }
