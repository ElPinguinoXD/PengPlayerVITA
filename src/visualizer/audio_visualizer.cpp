#include "audio_visualizer.hpp"

#include "audio/audio_player.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

constexpr int AudioVisualizer::kSampleCount;
constexpr int AudioVisualizer::kBandCount;

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

AudioVisualizer::AudioVisualizer() {
    for (int i = 0; i < kSampleCount; ++i) {
        window_[i] = 0.5f - 0.5f * std::cos((2.0f * kPi * i) / (kSampleCount - 1));
    }
}

void AudioVisualizer::update(const AudioPlayer& player) {
    std::array<float, kSampleCount> incoming{};

    if (player.isPlaying()) {
        player.getVisualizerSamples(incoming.data(), kSampleCount);
    }

    float currentPeak = 0.0f;
    for (int i = 0; i < kSampleCount; ++i) {
        // Ataque rapido y caida mas lenta para que la forma se sienta viva
        // sin parpadear con cada bloque PCM.
        const float target = incoming[i];
        waveform_[i] = waveform_[i] * 0.52f + target * 0.48f;
        currentPeak = std::max(currentPeak, std::fabs(waveform_[i]));
    }

    peak_ = std::max(currentPeak, peak_ * 0.90f);
    computeSpectrum(waveform_, player.sampleRate());
}

void AudioVisualizer::computeSpectrum(const std::array<float, kSampleCount>& samples, int sampleRate) {
    std::array<std::complex<float>, kSampleCount> fft{};

    for (int i = 0; i < kSampleCount; ++i) {
        fft[i] = std::complex<float>(samples[i] * window_[i], 0.0f);
    }

    // Bit reversal.
    for (int i = 1, j = 0; i < kSampleCount; ++i) {
        int bit = kSampleCount >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(fft[i], fft[j]);
    }

    // FFT radix-2 iterativa de 512 puntos.
    for (int len = 2; len <= kSampleCount; len <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < kSampleCount; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = fft[i + j];
                const std::complex<float> v = fft[i + j + len / 2] * w;
                fft[i + j] = u + v;
                fft[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    const int usableRate = sampleRate > 0 ? sampleRate : 44100;
    if (usableRate != cached_sample_rate_) {
        cached_sample_rate_ = usableRate;
        const int nyquistBin = kSampleCount / 2;
        const float binHz = static_cast<float>(usableRate) / kSampleCount;
        const float minHz = 80.0f;
        const float maxHz = std::min(16000.0f, usableRate * 0.48f);
        const float ratio = maxHz > minHz ? (maxHz / minHz) : 1.0f;

        for (int band = 0; band < kBandCount; ++band) {
            const float t0 = static_cast<float>(band) / kBandCount;
            const float t1 = static_cast<float>(band + 1) / kBandCount;
            const float hz0 = minHz * std::pow(ratio, t0);
            const float hz1 = minHz * std::pow(ratio, t1);
            int start = static_cast<int>(hz0 / binHz);
            int end = static_cast<int>(hz1 / binHz) + 1;
            start = std::max(1, std::min(nyquistBin - 1, start));
            end = std::max(start + 1, std::min(nyquistBin, end));
            band_start_[band] = start;
            band_end_[band] = end;
        }
    }

    for (int band = 0; band < kBandCount; ++band) {
        const int start = band_start_[band];
        const int end = band_end_[band];
        float energy = 0.0f;
        for (int bin = start; bin < end; ++bin) energy += std::abs(fft[bin]);
        energy /= static_cast<float>(std::max(1, end - start));

        // La raiz suaviza el enorme rango dinamico de una FFT cruda.
        float normalized = std::sqrt(std::max(0.0f, energy) / 20.0f);
        normalized = std::max(0.0f, std::min(1.0f, normalized));

        // Caida suave para evitar barras nerviosas.
        if (normalized >= bands_[band]) bands_[band] = bands_[band] * 0.35f + normalized * 0.65f;
        else bands_[band] = bands_[band] * 0.86f + normalized * 0.14f;
    }
}
