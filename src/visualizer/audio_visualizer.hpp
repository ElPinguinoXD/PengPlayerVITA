#pragma once

#include <array>

class AudioPlayer;

class AudioVisualizer {
public:
    static constexpr int kSampleCount = 512;
    static constexpr int kBandCount = 32;

    AudioVisualizer();

    void update(const AudioPlayer& player);

    const std::array<float, kSampleCount>& waveform() const { return waveform_; }
    const std::array<float, kBandCount>& bands() const { return bands_; }
    float peak() const { return peak_; }

private:
    void computeSpectrum(const std::array<float, kSampleCount>& samples, int sampleRate);

    std::array<float, kSampleCount> waveform_{};
    std::array<float, kSampleCount> window_{};
    std::array<float, kBandCount> bands_{};
    std::array<int, kBandCount> band_start_{};
    std::array<int, kBandCount> band_end_{};
    int cached_sample_rate_ = 0;
    float peak_ = 0.0f;
};
