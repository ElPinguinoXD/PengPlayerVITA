#pragma once

#include <psp2/types.h>
#include <atomic>

class BackgroundPlaybackManager {
public:
    BackgroundPlaybackManager();
    ~BackgroundPlaybackManager();

    bool initialize(bool enabled, bool preventAutoSuspend);
    void shutdown();

    void setEnabled(bool enabled);
    void setPreventAutoSuspend(bool enabled);
    void setAudioActive(bool active);

    bool enabled() const { return enabled_.load() != 0; }
    bool preventAutoSuspend() const { return prevent_auto_suspend_.load() != 0; }
    bool bgmPortAcquired() const { return bgm_acquired_.load() != 0; }
    int lastBgmResult() const { return last_bgm_result_.load(); }

    void requestDisplayOff();

private:
    static int powerThreadEntry(SceSize args, void* argp);
    int powerThreadRun();
    void refreshBgmPort();

    SceUID power_thread_id_;
    std::atomic<int> enabled_;
    std::atomic<int> prevent_auto_suspend_;
    std::atomic<int> audio_active_;
    std::atomic<int> bgm_acquired_;
    std::atomic<int> last_bgm_result_;
    std::atomic<int> stop_thread_;
};
