#include "VoiceUdp.h"

#include <sodium.h>

#include <QtGlobal>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define CLOSESOCK closesocket
    #define BAD_SOCKET INVALID_SOCKET
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define CLOSESOCK ::close
    #define BAD_SOCKET (-1)
#endif

namespace kestrel {

VoiceUdp::VoiceUdp()
    : m_socket(static_cast<Socket>(BAD_SOCKET)) {
#ifdef _WIN32
    static bool wsaInit = false;
    if (!wsaInit) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsaInit = true;
    }
#endif
    if (sodium_init() < 0) {
        // libsodium failure is fatal for voice; sends will simply fail
    }
}

VoiceUdp::~VoiceUdp() {
    close();
}

bool VoiceUdp::open(const std::string &ip, uint16_t port) {
    close();
    m_socket = static_cast<Socket>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (m_socket == static_cast<Socket>(BAD_SOCKET)) return false;
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    // "connect" a UDP socket: the OS then only delivers datagrams from the voice server
    if (::connect(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        CLOSESOCK(m_socket);
        m_socket = static_cast<Socket>(BAD_SOCKET);
        return false;
    }
    m_sequence = static_cast<uint16_t>(randombytes_uniform(65536));
    m_timestamp = randombytes_random();
    m_nonce = 0;
    m_running = true;
    m_thread = std::thread(&VoiceUdp::receiveLoop, this);
    return true;
}

void VoiceUdp::close() {
    if (m_rx) qInfo("Voice UDP: received %u packets, decrypted %u", m_rx.load(), m_decrypted.load());
    m_rx = 0;
    m_decrypted = 0;
    m_running = false;
    if (m_socket != static_cast<Socket>(BAD_SOCKET)) {
#ifdef _WIN32
        ::shutdown(m_socket, SD_BOTH);
#else
        ::shutdown(m_socket, SHUT_RDWR);
#endif
    }
    // the receive loop polls m_running every 250 ms; join before releasing the socket
    if (m_thread.joinable()) m_thread.join();
    if (m_socket != static_cast<Socket>(BAD_SOCKET)) {
        std::lock_guard lock(m_sendMutex);
        CLOSESOCK(m_socket);
        m_socket = static_cast<Socket>(BAD_SOCKET);
    }
    m_haveKey = false;
}

void VoiceUdp::discover(uint32_t ssrc) {
    uint8_t req[74] = {};
    req[0] = 0x00; req[1] = 0x01; // request
    req[2] = 0x00; req[3] = 70;   // length
    req[4] = (ssrc >> 24) & 0xFF;
    req[5] = (ssrc >> 16) & 0xFF;
    req[6] = (ssrc >> 8) & 0xFF;
    req[7] = ssrc & 0xFF;
    ::send(m_socket, reinterpret_cast<const char *>(req), sizeof(req), 0);
}

void VoiceUdp::setKey(const std::array<uint8_t, 32> &key) {
    std::lock_guard lock(m_sendMutex);
    m_key = key;
    m_haveKey = true;
}

void VoiceUdp::keepalive() {
    if (m_socket == static_cast<Socket>(BAD_SOCKET)) return;
    static const uint8_t data[] = { 0x13, 0x37 };
    ::send(m_socket, reinterpret_cast<const char *>(data), sizeof(data), 0);
}

void VoiceUdp::sendOpus(const uint8_t *data, size_t len) {
    if (!m_haveKey) return;
    std::lock_guard lock(m_sendMutex);
    if (m_socket == static_cast<Socket>(BAD_SOCKET)) return;

    m_sequence++;
    m_timestamp += 960; // 20 ms at 48 kHz
    m_nonce++;

    std::vector<uint8_t> packet(12 + len + crypto_aead_xchacha20poly1305_ietf_ABYTES + 4);
    packet[0] = 0x80;
    packet[1] = 0x78; // payload type 120 (opus)
    packet[2] = (m_sequence >> 8) & 0xFF;
    packet[3] = m_sequence & 0xFF;
    packet[4] = (m_timestamp >> 24) & 0xFF;
    packet[5] = (m_timestamp >> 16) & 0xFF;
    packet[6] = (m_timestamp >> 8) & 0xFF;
    packet[7] = m_timestamp & 0xFF;
    packet[8] = (m_ssrc >> 24) & 0xFF;
    packet[9] = (m_ssrc >> 16) & 0xFF;
    packet[10] = (m_ssrc >> 8) & 0xFF;
    packet[11] = m_ssrc & 0xFF;

    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    nonce[0] = (m_nonce >> 24) & 0xFF;
    nonce[1] = (m_nonce >> 16) & 0xFF;
    nonce[2] = (m_nonce >> 8) & 0xFF;
    nonce[3] = m_nonce & 0xFF;

    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(packet.data() + 12, &clen, data, len, packet.data(), 12, nullptr, nonce, m_key.data());
    packet.resize(12 + clen + 4);
    std::memcpy(packet.data() + 12 + clen, nonce, 4);
    ::send(m_socket, reinterpret_cast<const char *>(packet.data()), static_cast<int>(packet.size()), 0);
}

void VoiceUdp::receiveLoop() {
    std::vector<uint8_t> buf(4096);
    while (m_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_socket, &fds);
        timeval tv { 0, 250000 };
        const int r = ::select(static_cast<int>(m_socket) + 1, &fds, nullptr, nullptr, &tv);
        if (!m_running) break;
        if (r <= 0) continue;
        const int n = ::recv(m_socket, reinterpret_cast<char *>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        handlePacket(buf.data(), static_cast<size_t>(n));
    }
}

void VoiceUdp::handlePacket(const uint8_t *data, size_t len) {
    // IP discovery response
    if (len >= 74 && data[0] == 0x00 && data[1] == 0x02) {
        const char *ip = reinterpret_cast<const char *>(data + 8);
        const uint16_t port = static_cast<uint16_t>((data[72] << 8) | data[73]);
        if (onDiscovered) onDiscovered(std::string(ip, strnlen(ip, 64)), port);
        return;
    }
    if (!m_haveKey || len < 12 + 4 + crypto_aead_xchacha20poly1305_ietf_ABYTES) return;
    m_rx++;
    if (((data[0] >> 6) & 0x03) != 2) return; // RTP v2
    if ((data[1] & 0x7F) != 120) return;       // opus only (ignore RTCP etc.)

    const uint32_t ssrc = (uint32_t(data[8]) << 24) | (uint32_t(data[9]) << 16) | (uint32_t(data[10]) << 8) | data[11];
    if (ssrc == m_ssrc) return;

    const int csrc = data[0] & 0x0F;
    const bool extension = data[0] & 0x10;
    // rtpsize: the fixed header, CSRCs and the 4-byte extension preamble are
    // authenticated but unencrypted; the extension body is encrypted.
    size_t aadLen = 12 + csrc * 4 + (extension ? 4 : 0);
    if (len < aadLen + 4 + crypto_aead_xchacha20poly1305_ietf_ABYTES) return;

    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonce, data + len - 4, 4);

    std::vector<uint8_t> plain(len);
    unsigned long long plen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plen, nullptr, data + aadLen, len - aadLen - 4, data, aadLen, nonce, m_key.data()) != 0)
        return;
    m_decrypted++;

    size_t offset = 0;
    if (extension) {
        const size_t extWords = (size_t(data[12 + csrc * 4 + 2]) << 8) | data[12 + csrc * 4 + 3];
        offset = extWords * 4;
    }
    if (offset >= plen) return;
    if (onAudio) onAudio(ssrc, plain.data() + offset, static_cast<size_t>(plen) - offset);
}

} // namespace kestrel
