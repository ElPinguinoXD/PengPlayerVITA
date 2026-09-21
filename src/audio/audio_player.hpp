#pragma once

#include <psp2/types.h>
#include <array>
#include <atomic>
#include <string>

class AudioPlayer {
public:
    static constexpr int kVisualizerSamples = 512;

    enum class State {
        Stopped = 0,
        Loading,
        Playing,
        Paused,
        Error
    };

    AudioPlayer();
    ~AudioPlayer();

    bool playFile(const std::string& path, int startPositionMs = 0, bool startPaused = false);
    void stop();
    void togglePause();
    void seekRelative(int seconds);
    bool consumeTrackFinished();

    State state() const { return static_cast<State>(state_.load()); }
    bool hasTrack() const { return !current_path_.empty(); }
    bool isPlaying() const { return state() == State::Playing; }
    bool isPaused() const { return state() == State::Paused; }

    const std::string& currentPath() const { return current_path_; }
    const std::string& lastError() const { return last_error_; }

    int positionMs() const { return position_ms_.load(); }
    int durationMs() const { return duration_ms_.load(); }
    int sampleRate() const { return sample_rate_.load(); }
    int channels() const { return channels_.load(); }

    void getVisualizerSamples(float* out, int count) const;

    const char* stateLabel() const;

private:
    static int threadEntry(SceSize args, void* argp);
    int run();
    bool isSupportedSampleRate(int sampleRate) const;
    void setError(const std::string& message);

    SceUID thread_id_;
    std::string current_path_;
    std::string last_error_;

    std::atomic<int> state_;
    std::atomic<int> stop_requested_;
    std::atomic<int> pause_requested_;
    std::atomic<int> seek_request_ms_;
    std::atomic<int> finished_event_;

    std::atomic<int> position_ms_;
    std::atomic<int> duration_ms_;
    std::atomic<int> sample_rate_;
    std::atomic<int> channels_;
    std::array<std::atomic<int>, kVisualizerSamples> visual_samples_;
};
