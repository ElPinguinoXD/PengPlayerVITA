#include "background_playback_manager.hpp"

#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

namespace {
constexpr int kPowerThreadPriority = 0x10000100;
constexpr int kPowerThreadStack = 0x8000;
constexpr unsigned int kPowerTickIntervalUs = 1000 * 1000;
}

BackgroundPlaybackManager::BackgroundPlaybackManager()
    : power_thread_id_(-1),
      enabled_(0),
      prevent_auto_suspend_(0),
      audio_active_(0),
      bgm_acquired_(0),
      last_bgm_result_(0),
      stop_thread_(0) {}

BackgroundPlaybackManager::~BackgroundPlaybackManager() {
    shutdown();
}

bool BackgroundPlaybackManager::initialize(bool enabled, bool preventAutoSuspend) {
    enabled_.store(enabled ? 1 : 0);
    prevent_auto_suspend_.store(preventAutoSuspend ? 1 : 0);
    stop_thread_.store(0);

    // Mantener la propiedad BGM durante toda la sesion evita conflictos
    // al liberar/re-adquirir mientras SceAudioOut conserva
    // un puerto abierto (por ejemplo al pausar o restaurar una sesion).
    refreshBgmPort();

    if (power_thread_id_ < 0) {
        power_thread_id_ = sceKernelCreateThread(
            "PengPlayerPower",
            &BackgroundPlaybackManager::powerThreadEntry,
            kPowerThreadPriority,
            kPowerThreadStack,
            0,
            0,
            nullptr
        );
        if (power_thread_id_ >= 0) {
            BackgroundPlaybackManager* self = this;
            if (sceKernelStartThread(power_thread_id_, sizeof(self), &self) < 0) {
                sceKernelDeleteThread(power_thread_id_);
                power_thread_id_ = -1;
            }
        }
    }

    return !enabled || bgmPortAcquired();
}

void BackgroundPlaybackManager::shutdown() {
    if (power_thread_id_ >= 0) {
        stop_thread_.store(1);
        sceKernelWaitThreadEnd(power_thread_id_, nullptr, nullptr);
        sceKernelDeleteThread(power_thread_id_);
        power_thread_id_ = -1;
    }

    if (bgm_acquired_.exchange(0) != 0) {
        sceAppMgrReleaseBgmPort();
    }
}

void BackgroundPlaybackManager::setEnabled(bool enabled) {
    enabled_.store(enabled ? 1 : 0);
    refreshBgmPort();
}

void BackgroundPlaybackManager::setPreventAutoSuspend(bool enabled) {
    prevent_auto_suspend_.store(enabled ? 1 : 0);
}

void BackgroundPlaybackManager::setAudioActive(bool active) {
    // AudioActive controla el power tick; la propiedad del puerto BGM se
    // conserva mientras la opcion de segundo plano este habilitada.
    audio_active_.store(active ? 1 : 0);
}

void BackgroundPlaybackManager::requestDisplayOff() {
    if (audio_active_.load() != 0) {
        scePowerRequestDisplayOff();
    }
}

void BackgroundPlaybackManager::refreshBgmPort() {
    const bool shouldOwn = enabled_.load() != 0;

    if (shouldOwn) {
        if (bgm_acquired_.load() == 0) {
            const int result = sceAppMgrAcquireBgmPort();
            last_bgm_result_.store(result);
            if (result >= 0) bgm_acquired_.store(1);
        }
    } else if (bgm_acquired_.exchange(0) != 0) {
        const int result = sceAppMgrReleaseBgmPort();
        last_bgm_result_.store(result < 0 ? result : 0);
    } else if (!shouldOwn) {
        last_bgm_result_.store(0);
    }
}

int BackgroundPlaybackManager::powerThreadEntry(SceSize args, void* argp) {
    if (!argp || args != sizeof(BackgroundPlaybackManager*)) return -1;
    BackgroundPlaybackManager* self = *static_cast<BackgroundPlaybackManager**>(argp);
    return self ? self->powerThreadRun() : -1;
}

int BackgroundPlaybackManager::powerThreadRun() {
    while (stop_thread_.load() == 0) {
        // Solo cancelamos el temporizador de suspension total. No cancelamos
        // el apagado/atenuado de pantalla, asi la Vita puede apagar el display
        // mientras el audio sigue activo.
        if (enabled_.load() != 0 && prevent_auto_suspend_.load() != 0 && audio_active_.load() != 0) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }
        sceKernelDelayThread(kPowerTickIntervalUs);
    }
    return 0;
}
