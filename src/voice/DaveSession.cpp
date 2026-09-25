#include "DaveSession.h"

#include <dave/array_view.h>
#include <dave/logger.h>

#include <QDebug>

namespace kestrel {

// voice gateway opcodes used by DAVE
enum : int { MlsKeyPackage = 26, MlsCommitWelcome = 28 };

DaveSession::DaveSession(uint64_t channelId, uint64_t userId)
    : m_channelId(channelId)
    , m_userId(userId) {
    static bool sinkSet = false;
    if (!sinkSet) {
        sinkSet = true;
        discord::dave::SetLogSink([](discord::dave::LoggingSeverity sev, const char *file, int line, const std::string &msg) {
            if (sev == discord::dave::LS_ERROR || sev == discord::dave::LS_WARNING)
                qWarning().noquote() << "[DAVE]" << file << line << QString::fromStdString(msg);
        });
    }
    m_mls = discord::dave::mls::CreateSession(nullptr, "", [](const std::string &reason, const std::string &detail) {
        qWarning().noquote() << "[DAVE] MLS failure:" << QString::fromStdString(reason) << QString::fromStdString(detail);
    });
}

DaveSession::~DaveSession() = default;

void DaveSession::init(uint16_t version) {
    std::lock_guard lock(m_mutex);
    m_version = version;
    m_pendingVersion = version;
    reinit();
}

void DaveSession::reinit() {
    m_enabled = false;
    m_downgraded = false;
    const std::string self = std::to_string(m_userId);
    m_users.insert(self);
    m_mls->Init(m_version, m_channelId, self, m_transientKey);
    m_decryptors.clear();
    m_pendingReady = false;
    m_encryptor = discord::dave::CreateEncryptor();
    if (m_localSsrc) m_encryptor->AssignSsrcToCodec(m_localSsrc, discord::dave::Codec::Opus);
    const auto keyPackage = m_mls->GetMarshalledKeyPackage();
    if (!keyPackage.empty() && sendBinary) sendBinary(MlsKeyPackage, keyPackage);
}

void DaveSession::setLocalSsrc(uint32_t ssrc) {
    std::lock_guard lock(m_mutex);
    m_localSsrc = ssrc;
    if (m_encryptor) m_encryptor->AssignSsrcToCodec(ssrc, discord::dave::Codec::Opus);
}

void DaveSession::addUser(const std::string &id) {
    std::lock_guard lock(m_mutex);
    m_users.insert(id);
}

void DaveSession::removeUser(const std::string &id) {
    std::lock_guard lock(m_mutex);
    m_users.erase(id);
    const uint64_t uid = std::stoull(id);
    for (auto it = m_ssrcUsers.begin(); it != m_ssrcUsers.end(); ++it) {
        if (it->second == uid) {
            m_decryptors.erase(it->first);
            m_ssrcUsers.erase(it);
            break;
        }
    }
}

void DaveSession::mapSsrc(uint32_t ssrc, uint64_t userId) {
    std::lock_guard lock(m_mutex);
    m_ssrcUsers[ssrc] = userId;
    auto it = m_decryptors.find(ssrc);
    if (it != m_decryptors.end()) {
        if (auto ratchet = m_mls->GetKeyRatchet(std::to_string(userId))) it->second->TransitionToKeyRatchet(std::move(ratchet));
    }
}

void DaveSession::onExternalSender(const uint8_t *data, size_t size) {
    std::lock_guard lock(m_mutex);
    m_mls->SetExternalSender(std::vector<uint8_t>(data, data + size));
}

void DaveSession::onProposals(const uint8_t *data, size_t size) {
    std::lock_guard lock(m_mutex);
    auto response = m_mls->ProcessProposals(std::vector<uint8_t>(data, data + size), m_users);
    if (response && sendBinary) sendBinary(MlsCommitWelcome, *response);
}

void DaveSession::onCommitTransition(const uint8_t *data, size_t size) {
    if (size < 2) return;
    std::lock_guard lock(m_mutex);
    const int transitionId = (data[0] << 8) | data[1];
    auto result = m_mls->ProcessCommit(std::vector<uint8_t>(data + 2, data + size));
    if (std::holds_alternative<discord::dave::RosterMap>(result)) {
        m_pendingReady = true;
        if (sendReadyForTransition) sendReadyForTransition(transitionId);
        if (transitionId == 0) completeTransition();
    } else if (std::holds_alternative<discord::dave::failed_t>(result)) {
        if (sendInvalidCommitWelcome) sendInvalidCommitWelcome(transitionId);
        reinit();
    }
}

void DaveSession::onWelcome(const uint8_t *data, size_t size) {
    if (size < 2) return;
    std::lock_guard lock(m_mutex);
    const int transitionId = (data[0] << 8) | data[1];
    auto roster = m_mls->ProcessWelcome(std::vector<uint8_t>(data + 2, data + size), m_users);
    if (roster) {
        m_pendingReady = true;
        if (sendReadyForTransition) sendReadyForTransition(transitionId);
        if (transitionId == 0) completeTransition();
    } else {
        if (sendInvalidCommitWelcome) sendInvalidCommitWelcome(transitionId);
        reinit();
    }
}

void DaveSession::onPrepareTransition(int version, int transitionId) {
    std::lock_guard lock(m_mutex);
    m_pendingVersion = static_cast<uint16_t>(version);
    if (sendReadyForTransition) sendReadyForTransition(transitionId);
}

void DaveSession::onExecuteTransition(int transitionId) {
    std::lock_guard lock(m_mutex);
    if (m_pendingVersion != m_version) {
        m_version = m_pendingVersion;
        if (m_version == 0) {
            m_enabled = false;
            m_downgraded = true;
            if (stateChanged) stateChanged(false);
            return;
        }
    }
    if (!m_pendingReady) {
        qWarning() << "[DAVE] execute transition" << transitionId << "without pending commit; reinitialising";
        reinit();
        return;
    }
    completeTransition();
}

void DaveSession::onPrepareEpoch(int version, int epoch) {
    std::lock_guard lock(m_mutex);
    if (epoch == 1) {
        m_version = static_cast<uint16_t>(version);
        reinit();
    }
}

void DaveSession::completeTransition() {
    if (!m_pendingReady) return;
    m_pendingReady = false;
    if (auto ratchet = m_mls->GetKeyRatchet(std::to_string(m_userId))) m_encryptor->SetKeyRatchet(std::move(ratchet));
    for (auto &[ssrc, dec] : m_decryptors) {
        auto it = m_ssrcUsers.find(ssrc);
        if (it == m_ssrcUsers.end()) continue;
        if (auto ratchet = m_mls->GetKeyRatchet(std::to_string(it->second))) dec->TransitionToKeyRatchet(std::move(ratchet));
    }
    const bool was = m_enabled;
    m_enabled = true;
    m_downgraded = false;
    if (!was && stateChanged) stateChanged(true);
}

bool DaveSession::encrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out) {
    std::lock_guard lock(m_mutex);
    if (!m_encryptor) return false;
    out.resize(m_encryptor->GetMaxCiphertextByteSize(discord::dave::MediaType::Audio, len));
    size_t written = 0;
    const auto r = m_encryptor->Encrypt(discord::dave::MediaType::Audio, m_localSsrc, discord::dave::MakeArrayView(in, len),
                                        discord::dave::MakeArrayView(out.data(), out.size()), &written);
    if (r != discord::dave::IEncryptor::Success) return false;
    out.resize(written);
    return true;
}

bool DaveSession::decrypt(uint32_t ssrc, const uint8_t *in, size_t len, std::vector<uint8_t> &out) {
    std::lock_guard lock(m_mutex);
    auto it = m_decryptors.find(ssrc);
    if (it == m_decryptors.end()) {
        auto dec = discord::dave::CreateDecryptor();
        if (auto u = m_ssrcUsers.find(ssrc); u != m_ssrcUsers.end()) {
            if (auto ratchet = m_mls->GetKeyRatchet(std::to_string(u->second))) dec->TransitionToKeyRatchet(std::move(ratchet));
        }
        it = m_decryptors.emplace(ssrc, std::move(dec)).first;
    }
    out.resize(it->second->GetMaxPlaintextByteSize(discord::dave::MediaType::Audio, len));
    size_t written = 0;
    const auto r = it->second->Decrypt(discord::dave::MediaType::Audio, discord::dave::MakeArrayView(in, len),
                                       discord::dave::MakeArrayView(out.data(), out.size()), &written);
    if (r != discord::dave::IDecryptor::Success) return false;
    out.resize(written);
    return true;
}

} // namespace kestrel
