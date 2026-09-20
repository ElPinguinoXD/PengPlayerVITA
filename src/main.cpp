#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "browser.hpp"
#include "audio/audio_player.hpp"

namespace {
constexpr int SCREEN_W = 960;
constexpr int SCREEN_H = 544;
constexpr int LIST_TOP = 118;
constexpr int ROW_H = 31;
constexpr int VISIBLE_ROWS = 11;

unsigned int rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

const unsigned int BG = rgba(15, 16, 22);
const unsigned int PANEL = rgba(24, 26, 35);
const unsigned int PANEL_2 = rgba(31, 33, 44);
const unsigned int TEXT = rgba(245, 245, 248);
const unsigned int MUTED = rgba(160, 163, 177);
const unsigned int ACCENT = rgba(153, 102, 255);
const unsigned int ACCENT_SOFT = rgba(68, 47, 98);
const unsigned int ERROR_COLOR = rgba(255, 115, 115);

std::string shorten(const std::string& text, std::size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 3) return text.substr(0, maxChars);
    return text.substr(0, maxChars - 3) + "...";
}

std::string filenameFromPath(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string formatTime(int milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    const int totalSeconds = milliseconds / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    char out[16];
    std::snprintf(out, sizeof(out), "%d:%02d", minutes, seconds);
    return out;
}

void drawText(vita2d_pgf* font, int x, int y, float scale, unsigned int color, const std::string& text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
}

void drawProgress(const AudioPlayer& player) {
    constexpr int x = 620;
    constexpr int y = 508;
    constexpr int w = 270;
    constexpr int h = 5;

    vita2d_draw_rectangle(x, y, w, h, rgba(65, 67, 78));

    const int duration = player.durationMs();
    const int position = player.positionMs();
    if (duration > 0) {
        float progress = static_cast<float>(position) / static_cast<float>(duration);
        progress = std::max(0.0f, std::min(1.0f, progress));
        vita2d_draw_rectangle(x, y, static_cast<float>(w) * progress, h, ACCENT);
    }
}

void drawUi(vita2d_pgf* font, const MusicBrowser& browser,
            const std::string& selectedSong, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    vita2d_draw_rectangle(0, 0, SCREEN_W, 72, PANEL);
    drawText(font, 28, 39, 1.25f, TEXT, "PengPlayer");
    drawText(font, 28, 63, 0.72f, MUTED, "Biblioteca > Carpetas");

    vita2d_draw_rectangle(20, 82, 920, 27, PANEL_2);
    drawText(font, 32, 102, 0.68f, MUTED, shorten(browser.currentPath(), 88));

    const auto& entries = browser.entries();
    const int start = browser.scrollOffset();
    const int end = std::min(start + VISIBLE_ROWS, static_cast<int>(entries.size()));

    if (!browser.lastError().empty()) {
        drawText(font, 32, 151, 0.80f, ERROR_COLOR, browser.lastError());
        drawText(font, 32, 184, 0.70f, MUTED, "Pulsa O para volver a la carpeta anterior.");
    } else if (entries.empty()) {
        drawText(font, 32, 151, 0.82f, MUTED, "No hay carpetas ni archivos de audio compatibles aqui.");
    }

    for (int i = start; i < end; ++i) {
        const int row = i - start;
        const int y = LIST_TOP + row * ROW_H;
        const bool selected = (i == browser.selectedIndex());

        if (selected) {
            vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
        }

        const std::string icon = entries[i].isDirectory ? "[DIR]" : "[AUDIO]";
        drawText(font, 34, y + 20, 0.68f, entries[i].isDirectory ? ACCENT : MUTED, icon);
        drawText(font, 118, y + 20, 0.76f, TEXT, shorten(entries[i].name, 72));
    }

    // Mini-player real de 0.2
    vita2d_draw_rectangle(0, 472, SCREEN_W, 72, PANEL);
    vita2d_draw_rectangle(20, 486, 45, 45, ACCENT_SOFT);
    drawText(font, 34, 516, 0.78f, ACCENT,
             player.isPlaying() ? ">" : (player.isPaused() ? "||" : "*"));

    if (selectedSong.empty()) {
        drawText(font, 82, 504, 0.76f, TEXT, "Ninguna cancion seleccionada");
        drawText(font, 82, 526, 0.58f, MUTED,
                 "X: reproducir  |  O: atras  |  △: ordenar  |  START: salir");
    } else {
        drawText(font, 82, 498, 0.76f, TEXT, shorten(filenameFromPath(selectedSong), 52));

        if (player.state() == AudioPlayer::State::Error) {
            drawText(font, 82, 522, 0.58f, ERROR_COLOR, shorten(player.lastError(), 68));
        } else {
            std::string details = std::string(player.stateLabel());
            if (player.sampleRate() > 0) {
                details += "  |  " + std::to_string(player.sampleRate() / 1000.0f).substr(0, 4) + " kHz";
                details += player.channels() == 1 ? " mono" : " estereo";
            }
            drawText(font, 82, 522, 0.58f, MUTED, details);
        }

        drawProgress(player);
        drawText(font, 620, 531, 0.55f, MUTED,
                 formatTime(player.positionMs()) + " / " + formatTime(player.durationMs()));
        drawText(font, 742, 531, 0.52f, MUTED, "←/→ -5/+5s   L/R ant/sig   □ pausa");
    }

    drawText(font, 746, 39, 0.65f, MUTED, browser.ascending() ? "A-Z" : "Z-A");
    drawText(font, 811, 39, 0.65f, ACCENT, "v0.2");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}
}

int main() {
    sceIoMkdir("ux0:/music", 0777);

    vita2d_init();
    vita2d_set_clear_color(BG);
    vita2d_pgf* font = vita2d_load_default_pgf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    MusicBrowser browser("ux0:/music");
    AudioPlayer player;
    std::string selectedSong;

    SceCtrlData pad{};
    unsigned int previousButtons = 0;
    bool running = true;

    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const unsigned int pressed = pad.buttons & ~previousButtons;
        previousButtons = pad.buttons;

        if (pressed & SCE_CTRL_UP) browser.moveUp();
        if (pressed & SCE_CTRL_DOWN) browser.moveDown();

        if (pressed & SCE_CTRL_CROSS) {
            std::string previousSelection = selectedSong;
            if (browser.enterSelected(selectedSong)) {
                const bool sameActiveTrack =
                    selectedSong == player.currentPath() &&
                    (player.isPlaying() || player.isPaused());

                if (sameActiveTrack) {
                    player.togglePause();
                } else {
                    player.playFile(selectedSong);
                }
            } else if (selectedSong != previousSelection) {
                // Nothing: entering a directory does not alter current playback.
            }
        }

        if (pressed & SCE_CTRL_SQUARE) {
            player.togglePause();
        }

        if (pressed & SCE_CTRL_LEFT) {
            player.seekRelative(-5);
        }

        if (pressed & SCE_CTRL_RIGHT) {
            player.seekRelative(5);
        }

        if (pressed & SCE_CTRL_LTRIGGER) {
            if (browser.selectAdjacentAudio(-1, selectedSong)) {
                player.playFile(selectedSong);
            }
        }

        if (pressed & SCE_CTRL_RTRIGGER) {
            if (browser.selectAdjacentAudio(1, selectedSong)) {
                player.playFile(selectedSong);
            }
        }

        if (pressed & SCE_CTRL_CIRCLE) {
            browser.goBack();
        }

        if (pressed & SCE_CTRL_TRIANGLE) {
            browser.toggleSortDirection();
        }

        if (pressed & SCE_CTRL_START) {
            running = false;
        }

        drawUi(font, browser, selectedSong, player);
        sceKernelDelayThread(16000);
    }

    player.stop();
    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
