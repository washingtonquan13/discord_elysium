#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kestrel {

// RTP-over-UDP transport for voice with Discord's
// aead_xchacha20_poly1305_rtpsize transport encryption.
class VoiceUdp {
public:
    // called on the receive thread
    std::function<void(uint32_t ssrc, const uint8_t *opus, size_t len)> onAudio;
    std::function<void(const std::string &ip, uint16_t port)> onDiscovered;

    VoiceUdp();
    ~VoiceUdp();

    bool open(const std::string &ip, uint16_t port);
    void close();
    void discover(uint32_t ssrc);
    void setKey(const std::array<uint8_t, 32> &key);
    void setSsrc(uint32_t ssrc) { m_ssrc = ssrc; }
    void sendOpus(const uint8_t *data, size_t len); // thread-safe
    void keepalive();

private:
    void receiveLoop();
    void handlePacket(const uint8_t *data, size_t len);

#ifdef _WIN32
    using Socket = uintptr_t;
#else
    using Socket = int;
#endif
    Socket m_socket;
    std::atomic<bool> m_running { false };
    std::thread m_thread;
    std::mutex m_sendMutex;
    std::array<uint8_t, 32> m_key {};
    std::atomic<bool> m_haveKey { false };
    uint32_t m_ssrc = 0;
    uint16_t m_sequence = 0;
    uint32_t m_timestamp = 0;
    uint32_t m_nonce = 0;
    std::atomic<uint32_t> m_rx { 0 }, m_decrypted { 0 };
};

} // namespace kestrel
