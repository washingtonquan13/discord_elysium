#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace kestrel {

struct AudioDevice {
    std::string name;
    bool isDefault = false;
};

// Microphone capture -> Opus, and Opus -> mixed speaker output, using
// miniaudio (WASAPI on Windows). All real-time work happens on miniaudio's
// threads; the UI only reads a few atomics.
class AudioEngine {
public:
    // Called on the capture thread with each encoded 20 ms Opus frame.
    std::function<void(const uint8_t *data, size_t len)> onEncoded;
    // Called on the capture thread when we start/stop transmitting.
    std::function<void(bool speaking)> onSpeakingChanged;

    AudioEngine();
    ~AudioEngine();

    std::vector<AudioDevice> inputDevices();
    std::vector<AudioDevice> outputDevices();

    bool start(const std::string &input, const std::string &output);
    void stop();
    bool running() const { return m_running; }
    bool startMonitor(const std::string &input); // mic test without a call
    void stopMonitor();

    void feedOpus(uint32_t ssrc, const uint8_t *data, size_t len); // any thread
    void removeSource(uint32_t ssrc);

    void setMuted(bool v) { m_muted = v; }
    void setDeafened(bool v) { m_deafened = v; }
    void setPushToTalk(bool enabled, int key) { m_ptt = enabled; m_pttKey = key; }
    void setVadThreshold(float db) { m_vadThreshold = db; }
    void setInputGain(float g) { m_inputGain = g; }
    void setOutputGain(float g) { m_outputGain = g; }
    void setSourceVolume(uint32_t ssrc, float v);

    float inputLevelDb() const { return m_inputLevel; }
    bool transmitting() const { return m_transmitting; }
    std::vector<uint32_t> activeSources(); // SSRCs heard in the last ~300 ms

    // internal: miniaudio callbacks
    void processCapture(const float *in, uint32_t frames);
    void processPlayback(float *out, uint32_t frames);

private:
    struct Source {
        OpusDecoder *decoder = nullptr;
        std::deque<float> pcm; // interleaved stereo
        bool primed = false;
        float volume = 1.0f;
        int64_t lastActive = 0;
    };

    bool pttDown() const;
    void encodeFrame(const float *mono);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    OpusEncoder *m_encoder = nullptr;
    std::mutex m_mutex;
    std::map<uint32_t, Source> m_sources;
    std::map<uint32_t, float> m_volumes;
    std::vector<float> m_captureBuf;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_monitorOnly { false };
    std::atomic<bool> m_muted { false };
    std::atomic<bool> m_deafened { false };
    std::atomic<bool> m_ptt { false };
    std::atomic<int> m_pttKey { 0 };
    std::atomic<float> m_vadThreshold { -50.0f };
    std::atomic<float> m_inputGain { 1.0f };
    std::atomic<float> m_outputGain { 1.0f };
    std::atomic<float> m_inputLevel { -100.0f };
    std::atomic<bool> m_transmitting { false };
    int m_hangover = 0;
    int m_silenceFramesToSend = 0;
};

} // namespace kestrel
