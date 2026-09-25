#pragma once

#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <memory>

typedef struct evp_pkey_st EVP_PKEY;

namespace kestrel {

class Http;

// "Log in with QR code": the phone app scans a code containing a fingerprint
// of an RSA key we generate, then hands us the account token encrypted with it.
class RemoteAuth : public QObject {
    Q_OBJECT

public:
    RemoteAuth(const QString &url, Http *http, QObject *parent = nullptr);
    ~RemoteAuth() override;

    void start();
    void stop();

signals:
    void qrUrlChanged(const QString &url);
    void userScanned(const QString &username, const QString &avatarUrl);
    void tokenReceived(const QString &token);
    void failed(const QString &reason);

private:
    void onMessage(const QString &text);
    void sendJson(const QJsonObject &o);
    QByteArray decrypt(const QByteArray &ciphertext) const;
    QString publicKeyBase64() const;

    QString m_url;
    Http *m_http;
    QWebSocket *m_ws = nullptr;
    QTimer m_heartbeat;
    QTimer m_timeout;
    EVP_PKEY *m_key = nullptr;
    bool m_active = false;
};

} // namespace kestrel
