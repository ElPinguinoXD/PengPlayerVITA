#include "audio_player.hpp"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <sndfile.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

constexpr int AudioPlayer::kVisualizerSamples;

namespace {
// 2048 frames keeps enough headroom against underruns while keeping
// output latency reasonable. Startup delay is handled separately by avoiding
// libsndfile's full-file float peak scan.
constexpr int kAudioFrames = 2048; // Multiple of 64 as required by SceAudioOut.
constexpr int kNoSeekRequest = INT_MIN;
// Audio must not compete at the generic/default priority with rendering/UI.
// Vita priorities are inverse: a lower number means a higher priority.
constexpr int kThreadPriority = 0x40;
constexpr int kThreadStackSize = 0x40000;
}

AudioPlayer::AudioPlayer()
    : thread_id_(-1),
      state_(static_cast<int>(State::Stopped)),
      stop_requested_(0),
      pause_requested_(0),
      seek_request_ms_(kNoSeekRequest),
      finished_event_(0),
      position_ms_(0),
      duration_ms_(0),
      sample_rate_(0),
      channels_(0) {
    for (auto& sample : visual_samples_) sample.store(0);
}

AudioPlayer::~AudioPlayer() {
    stop();
}

bool AudioPlayer::playFile(const std::string& path, int startPositionMs, bool startPaused) {
    stop();

    current_path_ = path;
    last_error_.clear();
    position_ms_.store(0);
    duration_ms_.store(0);
    sample_rate_.store(0);
    channels_.store(0);
    for (auto& sample : visual_samples_) sample.store(0);
    stop_requested_.store(0);
    pause_requested_.store(startPaused ? 1 : 0);
    seek_request_ms_.store(startPositionMs > 0 ? startPositionMs : kNoSeekRequest);
    finished_event_.store(0);
    state_.store(static_cast<int>(State::Loading));

    thread_id_ = sceKernelCreateThread(
        "PengPlayerAudio",
        &AudioPlayer::threadEntry,
        kThreadPriority,
        kThreadStackSize,
        0,
        0,
        nullptr
    );

    if (thread_id_ < 0) {
        setError("No se pudo crear el hilo de audio.");
        return false;
    }

    AudioPlayer* self = this;
    const int result = sceKernelStartThread(thread_id_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(thread_id_);
        thread_id_ = -1;
        setError("No se pudo iniciar el hilo de audio.");
        return false;
    }

    return true;
}

void AudioPlayer::stop() {
    if (thread_id_ >= 0) {
        stop_requested_.store(1);
        pause_requested_.store(0);
        sceKernelWaitThreadEnd(thread_id_, nullptr, nullptr);
        sceKernelDeleteThread(thread_id_);
        thread_id_ = -1;
    }

    state_.store(static_cast<int>(State::Stopped));
    stop_requested_.store(0);
    pause_requested_.store(0);
    seek_request_ms_.store(kNoSeekRequest);
    finished_event_.store(0);
    position_ms_.store(0);
    duration_ms_.store(0);
    sample_rate_.store(0);
    channels_.store(0);
    for (auto& sample : visual_samples_) sample.store(0);
}

void AudioPlayer::togglePause() {
    const State current = state();
    if (current == State::Playing) {
        pause_requested_.store(1);
        state_.store(static_cast<int>(State::Paused));
    } else if (current == State::Paused) {
        pause_requested_.store(0);
        state_.store(static_cast<int>(State::Playing));
    }
}

void AudioPlayer::seekRelative(int seconds) {
    const State current = state();
    if (current != State::Playing && current != State::Paused) return;

    const int duration = duration_ms_.load();
    if (duration <= 0) return;

    int target = position_ms_.load() + seconds * 1000;
    target = std::max(0, std::min(duration, target));
    seek_request_ms_.store(target);
}

void AudioPlayer::getVisualizerSamples(float* out, int count) const {
    if (!out || count <= 0) return;
    const int limit = std::min(count, kVisualizerSamples);
    for (int i = 0; i < limit; ++i) {
        out[i] = static_cast<float>(visual_samples_[i].load()) / 32768.0f;
    }
    for (int i = limit; i < count; ++i) out[i] = 0.0f;
}

bool AudioPlayer::consumeTrackFinished() {
    return finished_event_.exchange(0) != 0;
}

const char* AudioPlayer::stateLabel() const {
    switch (state()) {
        case State::Loading: return "Cargando";
        case State::Playing: return "Reproduciendo";
        case State::Paused:  return "Pausado";
        case State::Error:   return "Error";
        case State::Stopped:
        default:             return "Detenido";
    }
}

int AudioPlayer::threadEntry(SceSize args, void* argp) {
    if (!argp || args != sizeof(AudioPlayer*)) return -1;
    AudioPlayer* self = *static_cast<AudioPlayer**>(argp);
    return self ? self->run() : -1;
}

bool AudioPlayer::isSupportedSampleRate(int rate) const {
    switch (rate) {
        case 8000:
        case 11025:
        case 12000:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
            return true;
        default:
            return false;
    }
}

void AudioPlayer::setError(const std::string& message) {
    last_error_ = message;
    state_.store(static_cast<int>(State::Error));
}

int AudioPlayer::run() {
    SF_INFO info;
    std::memset(&info, 0, sizeof(info));

    SNDFILE* file = sf_open(current_path_.c_str(), SFM_READ, &info);
    if (!file) {
        setError(std::string("No se pudo abrir el audio: ") + sf_strerror(nullptr));
        return -1;
    }

    // Keep libsndfile clipping enabled, but DO NOT use
    // SFC_SET_SCALE_FLOAT_INT_READ here. Enabling that command can force
    // libsndfile to scan the whole file to calculate the signal maximum,
    // which caused ~10 second startup times on Vita. Float/double PCM is
    // converted manually below instead.
    sf_command(file, SFC_SET_CLIPPING, nullptr, SF_TRUE);

    const int subtype = info.format & SF_FORMAT_SUBMASK;
    const bool floatingPointSource =
        subtype == SF_FORMAT_FLOAT || subtype == SF_FORMAT_DOUBLE;

    if (info.channels != 1 && info.channels != 2) {
        sf_close(file);
        setError("PengPlayer admite por ahora audio mono o estereo.");
        return -1;
    }

    if (!isSupportedSampleRate(info.samplerate)) {
        sf_close(file);
        setError("Frecuencia no compatible aun. PengPlayer admite por ahora hasta 48 kHz.");
        return -1;
    }

    sample_rate_.store(info.samplerate);
    channels_.store(info.channels);

    if (info.frames > 0 && info.samplerate > 0) {
        const long long duration = (static_cast<long long>(info.frames) * 1000LL) / info.samplerate;
        duration_ms_.store(static_cast<int>(std::min<long long>(duration, INT_MAX)));
    }

    const SceAudioOutMode mode = info.channels == 1
        ? SCE_AUDIO_OUT_MODE_MONO
        : SCE_AUDIO_OUT_MODE_STEREO;

    const int port = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_BGM,
        kAudioFrames,
        info.samplerate,
        mode
    );

    if (port < 0) {
        sf_close(file);
        setError("SceAudioOut no pudo abrir el puerto BGM.");
        return -1;
    }

    // We want the player's PCM without the system BGM dynamic normalizer
    // changing the level while debugging audio quality.
    sceAudioOutSetAlcMode(SCE_AUDIO_ALC_OFF);

    std::vector<short> buffer(static_cast<std::size_t>(kAudioFrames * info.channels), 0);
    std::vector<float> floatBuffer;
    if (floatingPointSource) {
        floatBuffer.resize(static_cast<std::size_t>(kAudioFrames * info.channels));
    }

    sf_count_t currentFrame = 0;
    state_.store(static_cast<int>(pause_requested_.load() ? State::Paused : State::Playing));

    while (!stop_requested_.load()) {
        const int seekMs = seek_request_ms_.exchange(kNoSeekRequest);
        if (seekMs != kNoSeekRequest) {
            const sf_count_t targetFrame =
                static_cast<sf_count_t>((static_cast<long long>(seekMs) * info.samplerate) / 1000LL);
            const sf_count_t result = sf_seek(file, targetFrame, SEEK_SET);
            if (result >= 0) {
                currentFrame = result;
                position_ms_.store(
                    static_cast<int>((static_cast<long long>(currentFrame) * 1000LL) / info.samplerate)
                );
            }
        }

        if (pause_requested_.load()) {
            sceKernelDelayThread(10000);
            continue;
        }

        sf_count_t framesRead = 0;

        if (floatingPointSource) {
            framesRead = sf_readf_float(file, floatBuffer.data(), kAudioFrames);

            if (framesRead > 0) {
                const std::size_t sampleCount =
                    static_cast<std::size_t>(framesRead * info.channels);

                for (std::size_t i = 0; i < sampleCount; ++i) {
                    const float sample = std::max(-1.0f, std::min(1.0f, floatBuffer[i]));

                    if (sample <= -1.0f) {
                        buffer[i] = static_cast<short>(-32768);
                    } else if (sample >= 1.0f) {
                        buffer[i] = static_cast<short>(32767);
                    } else {
                        buffer[i] = static_cast<short>(std::lrintf(sample * 32767.0f));
                    }
                }
            }
        } else {
            framesRead = sf_readf_short(file, buffer.data(), kAudioFrames);
        }

        if (framesRead <= 0) break;

        if (framesRead < kAudioFrames) {
            const std::size_t firstSilentSample =
                static_cast<std::size_t>(framesRead * info.channels);
            std::fill(buffer.begin() + firstSilentSample, buffer.end(), 0);
        }

        // Publicamos una copia mono reducida del bloque PCM para los
        // visualizadores. No toca el audio que se envia a SceAudioOut.
        const int availableFrames = static_cast<int>(framesRead);
        for (int i = 0; i < kVisualizerSamples; ++i) {
            int mono = 0;
            if (availableFrames > 0) {
                int frame = (i * availableFrames) / kVisualizerSamples;
                if (frame >= availableFrames) frame = availableFrames - 1;
                if (info.channels == 1) {
                    mono = buffer[frame];
                } else {
                    const int left = buffer[frame * 2];
                    const int right = buffer[frame * 2 + 1];
                    mono = (left + right) / 2;
                }
            }
            visual_samples_[i].store(mono);
        }

        const int outputResult = sceAudioOutOutput(port, buffer.data());
        if (outputResult < 0) {
            sceAudioOutReleasePort(port);
            sf_close(file);
            setError("Error enviando PCM a SceAudioOut.");
            return -1;
        }

        currentFrame += framesRead;
        position_ms_.store(
            static_cast<int>((static_cast<long long>(currentFrame) * 1000LL) / info.samplerate)
        );

        if (framesRead < kAudioFrames) break;
    }

    // Wait until the last submitted block has left the output queue.
    sceAudioOutOutput(port, nullptr);
    sceAudioOutReleasePort(port);
    sf_close(file);
    for (auto& sample : visual_samples_) sample.store(0);

    if (!stop_requested_.load()) {
        const int duration = duration_ms_.load();
        if (duration > 0) position_ms_.store(duration);
        state_.store(static_cast<int>(State::Stopped));
        // One-shot event consumed by the UI thread. This lets PengPlayer
        // distinguish a natural end-of-track from a manual stop/change.
        finished_event_.store(1);
    }

    return 0;
}
