#include "AudioEngine.h"

#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>
#include <opus.h>

#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace kestrel {

static constexpr int kRate = 48000;
static constexpr int kFrame = 960; // 20 ms
static constexpr size_t kMaxBuffered = kFrame * 2 * 10; // 200 ms stereo
static constexpr size_t kPrime = kFrame * 2 * 2;        // 40 ms before playout starts

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct AudioEngine::Impl {
    ma_context context {};
    bool contextOk = false;
    ma_device capture {};
    ma_device playback {};
    bool captureOk = false;
    bool playbackOk = false;
};

static void captureCallback(ma_device *dev, void *, const void *input, ma_uint32 frames) {
    static_cast<AudioEngine *>(dev->pUserData)->processCapture(static_cast<const float *>(input), frames);
}

static void playbackCallback(ma_device *dev, void *output, const void *, ma_uint32 frames) {
    static_cast<AudioEngine *>(dev->pUserData)->processPlayback(static_cast<float *>(output), frames);
}

AudioEngine::AudioEngine()
    : m_impl(std::make_unique<Impl>()) {
    if (qEnvironmentVariableIsSet("KESTREL_AUDIO_NULL")) {
        // headless testing: a virtual device that produces/consumes silence
        ma_backend backends[] = { ma_backend_null };
        m_impl->contextOk = ma_context_init(backends, 1, nullptr, &m_impl->context) == MA_SUCCESS;
    } else {
        m_impl->contextOk = ma_context_init(nullptr, 0, nullptr, &m_impl->context) == MA_SUCCESS;
    }
    int err = 0;
    m_encoder = opus_encoder_create(kRate, 2, OPUS_APPLICATION_VOIP, &err);
    if (m_encoder) {
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(64000));
        opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(5));
        opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    }
}

AudioEngine::~AudioEngine() {
    stop();
    stopMonitor();
    if (m_encoder) opus_encoder_destroy(m_encoder);
    for (auto &[ssrc, s] : m_sources) opus_decoder_destroy(s.decoder);
    if (m_impl->contextOk) ma_context_uninit(&m_impl->context);
}

static std::vector<AudioDevice> listDevices(ma_context *ctx, bool capture) {
    std::vector<AudioDevice> out;
    ma_device_info *playbackInfos = nullptr, *captureInfos = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) return out;
    ma_device_info *infos = capture ? captureInfos : playbackInfos;
    const ma_uint32 count = capture ? captureCount : playbackCount;
    for (ma_uint32 i = 0; i < count; i++) out.push_back({ infos[i].name, infos[i].isDefault != 0 });
    return out;
}

std::vector<AudioDevice> AudioEngine::inputDevices() {
    return m_impl->contextOk ? listDevices(&m_impl->context, true) : std::vector<AudioDevice> {};
}

std::vector<AudioDevice> AudioEngine::outputDevices() {
    return m_impl->contextOk ? listDevices(&m_impl->context, false) : std::vector<AudioDevice> {};
}

static bool findDevice(ma_context *ctx, bool capture, const std::string &name, ma_device_id &id) {
    if (name.empty()) return false;
    ma_device_info *playbackInfos = nullptr, *captureInfos = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) return false;
    ma_device_info *infos = capture ? captureInfos : playbackInfos;
    const ma_uint32 count = capture ? captureCount : playbackCount;
    for (ma_uint32 i = 0; i < count; i++) {
        if (name == infos[i].name) {
            id = infos[i].id;
            return true;
        }
    }
    return false;
}

static bool openCapture(AudioEngine *self, ma_context *ctx, ma_device *dev, const std::string &input) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = kRate;
    cfg.periodSizeInFrames = kFrame / 2;
    cfg.dataCallback = captureCallback;
    cfg.pUserData = self;
    ma_device_id id;
    if (findDevice(ctx, true, input, id)) cfg.capture.pDeviceID = &id;
    if (ma_device_init(ctx, &cfg, dev) != MA_SUCCESS) return false;
    if (ma_device_start(dev) != MA_SUCCESS) {
        ma_device_uninit(dev);
        return false;
    }
    return true;
}

bool AudioEngine::start(const std::string &input, const std::string &output) {
    stop();
    stopMonitor();
    if (!m_impl->contextOk || !m_encoder) return false;
    m_monitorOnly = false;
    m_captureBuf.clear();
    m_transmitting = false;
    m_hangover = 0;

    m_impl->captureOk = openCapture(this, &m_impl->context, &m_impl->capture, input);

    ma_device_config pcfg = ma_device_config_init(ma_device_type_playback);
    pcfg.playback.format = ma_format_f32;
    pcfg.playback.channels = 2;
    pcfg.sampleRate = kRate;
    pcfg.periodSizeInFrames = kFrame / 2;
    pcfg.dataCallback = playbackCallback;
    pcfg.pUserData = this;
    ma_device_id pid;
    if (findDevice(&m_impl->context, false, output, pid)) pcfg.playback.pDeviceID = &pid;
    m_impl->playbackOk = ma_device_init(&m_impl->context, &pcfg, &m_impl->playback) == MA_SUCCESS
        && ma_device_start(&m_impl->playback) == MA_SUCCESS;

    m_running = true;
    qInfo("Audio started: capture=%d playback=%d", m_impl->captureOk, m_impl->playbackOk);
    return m_impl->captureOk || m_impl->playbackOk;
}

void AudioEngine::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_impl->captureOk) ma_device_uninit(&m_impl->capture);
    if (m_impl->playbackOk) ma_device_uninit(&m_impl->playback);
    m_impl->captureOk = m_impl->playbackOk = false;
    std::lock_guard lock(m_mutex);
    for (auto &[ssrc, s] : m_sources) opus_decoder_destroy(s.decoder);
    m_sources.clear();
    m_transmitting = false;
}

bool AudioEngine::startMonitor(const std::string &input) {
    if (m_running) return true; // already measuring during a call
    stopMonitor();
    m_monitorOnly = true;
    m_captureBuf.clear();
    m_impl->captureOk = openCapture(this, &m_impl->context, &m_impl->capture, input);
    return m_impl->captureOk;
}

void AudioEngine::stopMonitor() {
    if (!m_monitorOnly) return;
    m_monitorOnly = false;
    if (m_impl->captureOk && !m_running) {
        ma_device_uninit(&m_impl->capture);
        m_impl->captureOk = false;
    }
    m_inputLevel = -100.0f;
}

bool AudioEngine::pttDown() const {
#ifdef _WIN32
    const int key = m_pttKey;
    return key > 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
#else
    return false;
#endif
}

void AudioEngine::processCapture(const float *in, uint32_t frames) {
    if (!in) return;
    const float gain = m_inputGain;
    for (uint32_t i = 0; i < frames; i++) m_captureBuf.push_back(in[i] * gain);

    while (m_captureBuf.size() >= static_cast<size_t>(kFrame)) {
        float frame[kFrame];
        std::memcpy(frame, m_captureBuf.data(), sizeof(frame));
        m_captureBuf.erase(m_captureBuf.begin(), m_captureBuf.begin() + kFrame);

        double sum = 0;
        for (float s : frame) sum += double(s) * s;
        const float rms = static_cast<float>(std::sqrt(sum / kFrame));
        const float db = rms > 1e-6f ? 20.0f * std::log10(rms) : -100.0f;
        m_inputLevel = db;

        if (m_monitorOnly) continue;

        bool transmit;
        if (m_muted || m_deafened) {
            transmit = false;
            m_hangover = 0;
        } else if (m_ptt) {
            transmit = pttDown();
        } else {
            if (db > m_vadThreshold) m_hangover = 15; // keep sending 300 ms after speech stops
            transmit = m_hangover > 0;
            if (m_hangover > 0) m_hangover--;
        }

        if (transmit != m_transmitting) {
            m_transmitting = transmit;
            if (onSpeakingChanged) onSpeakingChanged(transmit);
            if (!transmit) m_silenceFramesToSend = 5;
        }
        if (transmit) {
            encodeFrame(frame);
        } else if (m_silenceFramesToSend > 0) {
            // Discord expects a few Opus silence frames when a speaker stops
            static const uint8_t silence[] = { 0xF8, 0xFF, 0xFE };
            m_silenceFramesToSend--;
            if (onEncoded) onEncoded(silence, sizeof(silence));
        }
    }
}

void AudioEngine::encodeFrame(const float *mono) {
    float stereo[kFrame * 2];
    for (int i = 0; i < kFrame; i++) {
        const float s = std::clamp(mono[i], -1.0f, 1.0f);
        stereo[i * 2] = s;
        stereo[i * 2 + 1] = s;
    }
    uint8_t packet[1500];
    const int n = opus_encode_float(m_encoder, stereo, kFrame, packet, sizeof(packet));
    if (n > 0 && onEncoded) onEncoded(packet, static_cast<size_t>(n));
}

void AudioEngine::feedOpus(uint32_t ssrc, const uint8_t *data, size_t len) {
    if (!m_running) return;
    float pcm[kFrame * 2 * 6]; // up to 120 ms
    std::lock_guard lock(m_mutex);
    Source &src = m_sources[ssrc];
    if (!src.decoder) {
        int err = 0;
        src.decoder = opus_decoder_create(kRate, 2, &err);
        if (!src.decoder) return;
        if (auto it = m_volumes.find(ssrc); it != m_volumes.end()) src.volume = it->second;
    }
    const int samples = opus_decode_float(src.decoder, data, static_cast<opus_int32>(len), pcm, kFrame * 6, 0);
    if (samples <= 0) return;

    double sum = 0;
    for (int i = 0; i < samples * 2; i++) sum += double(pcm[i]) * pcm[i];
    if (std::sqrt(sum / (samples * 2)) > 0.004) src.lastActive = nowMs();

    src.pcm.insert(src.pcm.end(), pcm, pcm + samples * 2);
    if (src.pcm.size() > kMaxBuffered) {
        // we're falling behind (clock drift / burst): drop the oldest audio
        src.pcm.erase(src.pcm.begin(), src.pcm.begin() + (src.pcm.size() - kPrime));
    }
}

void AudioEngine::removeSource(uint32_t ssrc) {
    std::lock_guard lock(m_mutex);
    auto it = m_sources.find(ssrc);
    if (it == m_sources.end()) return;
    opus_decoder_destroy(it->second.decoder);
    m_sources.erase(it);
}

void AudioEngine::setSourceVolume(uint32_t ssrc, float v) {
    std::lock_guard lock(m_mutex);
    m_volumes[ssrc] = v;
    if (auto it = m_sources.find(ssrc); it != m_sources.end()) it->second.volume = v;
}

std::vector<uint32_t> AudioEngine::activeSources() {
    std::vector<uint32_t> out;
    const int64_t now = nowMs();
    std::lock_guard lock(m_mutex);
    for (auto &[ssrc, s] : m_sources)
        if (now - s.lastActive < 300) out.push_back(ssrc);
    return out;
}

void AudioEngine::processPlayback(float *out, uint32_t frames) {
    const size_t samples = static_cast<size_t>(frames) * 2;
    std::fill(out, out + samples, 0.0f);
    std::lock_guard lock(m_mutex);
    const bool deaf = m_deafened;
    const float master = m_outputGain;
    for (auto &[ssrc, s] : m_sources) {
        if (!s.primed) {
            if (s.pcm.size() < kPrime) continue;
            s.primed = true;
        }
        const size_t n = std::min(samples, s.pcm.size());
        if (!deaf) {
            const float g = s.volume * master;
            for (size_t i = 0; i < n; i++) out[i] += s.pcm[i] * g;
        }
        s.pcm.erase(s.pcm.begin(), s.pcm.begin() + n);
        if (s.pcm.empty()) s.primed = false; // underrun: re-buffer before resuming
    }
    for (size_t i = 0; i < samples; i++) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

} // namespace kestrel
