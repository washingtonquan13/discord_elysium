#pragma once
// DAVE: Discord's audio/video end-to-end encryption, built on MLS.
// Adapted from Abaddon's implementation (GPL-3.0), using Discord's libdave.

#include <dave/dave_interfaces.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mlspp {
class SignaturePrivateKey;
}

namespace kestrel {

class DaveSession {
public:
    std::function<void(int opcode, const std::vector<uint8_t> &payload)> sendBinary;
    std::function<void(int transitionId)> sendReadyForTransition;
    std::function<void(int transitionId)> sendInvalidCommitWelcome;
    std::function<void(bool enabled)> stateChanged;

    DaveSession(uint64_t channelId, uint64_t userId);
    ~DaveSession();

    void init(uint16_t version);
    void setLocalSsrc(uint32_t ssrc);
    void addUser(const std::string &id);
    void removeUser(const std::string &id);
    void mapSsrc(uint32_t ssrc, uint64_t userId);

    void onExternalSender(const uint8_t *data, size_t size);
    void onProposals(const uint8_t *data, size_t size);
    void onCommitTransition(const uint8_t *data, size_t size);
    void onWelcome(const uint8_t *data, size_t size);
    void onPrepareTransition(int version, int transitionId);
    void onExecuteTransition(int transitionId);
    void onPrepareEpoch(int version, int epoch);

    bool enabled() const { return m_enabled; }
    bool downgraded() const { return m_downgraded; }

    // Thread-safe: called from the audio capture thread / UDP receive thread.
    bool encrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out);
    bool decrypt(uint32_t ssrc, const uint8_t *in, size_t len, std::vector<uint8_t> &out);

private:
    void reinit();
    void completeTransition();

    std::recursive_mutex m_mutex;
    std::unique_ptr<discord::dave::mls::ISession> m_mls;
    std::unique_ptr<discord::dave::IEncryptor> m_encryptor;
    std::unordered_map<uint32_t, std::unique_ptr<discord::dave::IDecryptor>> m_decryptors;
    std::unordered_map<uint32_t, uint64_t> m_ssrcUsers;
    std::set<std::string> m_users;
    std::shared_ptr<::mlspp::SignaturePrivateKey> m_transientKey;
    uint64_t m_channelId;
    uint64_t m_userId;
    uint32_t m_localSsrc = 0;
    uint16_t m_version = 0;
    uint16_t m_pendingVersion = 0;
    bool m_enabled = false;
    bool m_downgraded = false;
    bool m_pendingReady = false;
};

} // namespace kestrel
