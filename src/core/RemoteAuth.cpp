#include "RemoteAuth.h"

#include "Http.h"
#include "Json.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

namespace kestrel {

using namespace json;

RemoteAuth::RemoteAuth(const QString &url, Http *http, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_http(http) {
    connect(&m_heartbeat, &QTimer::timeout, this, [this]() { sendJson({ { "op", "heartbeat" } }); });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        // the QR code expired; start over with a fresh one
        if (m_active) start();
    });
}

RemoteAuth::~RemoteAuth() {
    stop();
    if (m_key) EVP_PKEY_free(m_key);
}

void RemoteAuth::start() {
    stop();
    m_active = true;

    if (m_key) EVP_PKEY_free(m_key);
    m_key = nullptr;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (ctx && EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) > 0) {
        EVP_PKEY_keygen(ctx, &m_key);
    }
    EVP_PKEY_CTX_free(ctx);
    if (!m_key) {
        emit failed("Could not generate an encryption key.");
        return;
    }

    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &RemoteAuth::onMessage);
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        m_heartbeat.stop();
        if (m_active) {
            qInfo() << "Remote auth socket closed:" << (m_ws ? m_ws->closeCode() : 0);
            // reconnect for a fresh code unless we're done
            QTimer::singleShot(2000, this, [this]() { if (m_active) start(); });
        }
    });
    QNetworkRequest req { QUrl(m_url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    req.setRawHeader("Origin", "https://discord.com");
    m_ws->open(req);
}

void RemoteAuth::stop() {
    m_active = false;
    m_heartbeat.stop();
    m_timeout.stop();
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

void RemoteAuth::sendJson(const QJsonObject &o) {
    if (m_ws) m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

QString RemoteAuth::publicKeyBase64() const {
    unsigned char *der = nullptr;
    const int len = i2d_PUBKEY(m_key, &der);
    if (len <= 0) return {};
    QByteArray bytes(reinterpret_cast<const char *>(der), len);
    OPENSSL_free(der);
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray RemoteAuth::decrypt(const QByteArray &ciphertext) const {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(m_key, nullptr);
    QByteArray out;
    size_t outlen = 0;
    if (ctx && EVP_PKEY_decrypt_init(ctx) > 0 && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0
        && EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) > 0 && EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) > 0) {
        const auto *in = reinterpret_cast<const unsigned char *>(ciphertext.constData());
        if (EVP_PKEY_decrypt(ctx, nullptr, &outlen, in, ciphertext.size()) > 0) {
            out.resize(static_cast<int>(outlen));
            if (EVP_PKEY_decrypt(ctx, reinterpret_cast<unsigned char *>(out.data()), &outlen, in, ciphertext.size()) > 0) {
                out.resize(static_cast<int>(outlen));
            } else {
                out.clear();
            }
        }
    }
    EVP_PKEY_CTX_free(ctx);
    return out;
}

void RemoteAuth::onMessage(const QString &text) {
    const auto msg = QJsonDocument::fromJson(text.toUtf8()).object();
    const auto op = str(msg, "op");

    if (op == "hello") {
        m_heartbeat.start(i32(msg, "heartbeat_interval", 41250));
        m_timeout.start(i32(msg, "timeout_ms", 120000));
        sendJson({ { "op", "init" }, { "encoded_public_key", publicKeyBase64() } });
    } else if (op == "nonce_proof") {
        const auto nonce = decrypt(QByteArray::fromBase64(str(msg, "encrypted_nonce").toLatin1()));
        const auto proof = nonce.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        sendJson({ { "op", "nonce_proof" }, { "nonce", QString::fromLatin1(proof) } });
    } else if (op == "pending_remote_init") {
        emit qrUrlChanged("https://discord.com/ra/" + str(msg, "fingerprint"));
    } else if (op == "pending_ticket") {
        const auto payload = QString::fromUtf8(decrypt(QByteArray::fromBase64(str(msg, "encrypted_user_payload").toLatin1())));
        // user_id:discriminator:avatar_hash:username
        const auto parts = payload.split(':');
        QString name, avatar;
        if (parts.size() >= 4) {
            name = parts.mid(3).join(':');
            if (!parts[2].isEmpty() && parts[2] != "0")
                avatar = QString("https://cdn.discordapp.com/avatars/%1/%2.png?size=128").arg(parts[0], parts[2]);
        }
        emit userScanned(name, avatar);
    } else if (op == "pending_login") {
        const auto ticket = str(msg, "ticket");
        m_http->post("/users/@me/remote-auth/login", { { "ticket", ticket } }, [this](const HttpResponse &r) {
            const auto encrypted = str(r.json.object(), "encrypted_token");
            if (!r.ok() || encrypted.isEmpty()) {
                const auto body = r.json.object();
                if (has(body, "captcha_key"))
                    emit failed("Discord asked for a captcha. Log in once in a web browser, then try again.");
                else
                    emit failed("Login was rejected (" + QString::number(r.status) + "). Try again.");
                return;
            }
            const auto token = QString::fromUtf8(decrypt(QByteArray::fromBase64(encrypted.toLatin1())));
            if (token.isEmpty()) {
                emit failed("Could not decrypt the login token.");
                return;
            }
            stop();
            emit tokenReceived(token);
        });
    } else if (op == "cancel") {
        emit failed("Login was cancelled on your phone.");
        start();
    }
}

} // namespace kestrel
