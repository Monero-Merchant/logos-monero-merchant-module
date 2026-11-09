#include "payment_qr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <qrencode.h>
static QString encodeQr(const QString& payload) {
    const auto bytes = payload.toUtf8();
    QRcode* code = QRcode_encodeString8bit(bytes.constData(), 0, QR_ECLEVEL_M);
    if (!code) return QStringLiteral("{\"ok\":false,\"error\":\"Could not generate payment QR.\"}");
    QJsonArray runs;
    for (int y = 0; y < code->width; ++y) {
        for (int x = 0; x < code->width;) {
            if (!(code->data[y * code->width + x] & 1)) { ++x; continue; }
            const int start = x;
            while (x < code->width && (code->data[y * code->width + x] & 1)) ++x;
            runs.append(QJsonArray{start, y, x - start});
        }
    }
    const QJsonObject result{{"ok", true}, {"size", code->width}, {"runs", runs}};
    QRcode_free(code);
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}
QString encodePaymentQr(const QString& paymentUri) {
    if (!paymentUri.startsWith("monero:") || paymentUri.size() > 512)
        return QStringLiteral("{\"ok\":false,\"error\":\"Invalid Monero payment URI.\"}");
    return encodeQr(paymentUri);
}
QString encodePairingQr(const QString& invitation) {
    const auto value = QJsonDocument::fromJson(invitation.toUtf8()).object();
    if (invitation.size() > 8192 || value.value("version").toInt() != 2
        || value.value("protocol").toString() != "/monero-merchant/delivery/2.0.0"
        || value.value("secret").toString().size() != 43)
        return QStringLiteral("{\"ok\":false,\"error\":\"Invalid pairing invitation.\"}");
    return encodeQr(invitation);
}
