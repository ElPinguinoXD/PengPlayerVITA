#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "browser.hpp"
#include "audio/audio_player.hpp"
#include "media/metadata.hpp"
#include "media/cover_art.hpp"

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

std::string formatKhz(int sampleRate) {
    if (sampleRate <= 0) return "";
    char out[24];
    if (sampleRate % 1000 == 0) {
        std::snprintf(out, sizeof(out), "%d kHz", sampleRate / 1000);
    } else {
        std::snprintf(out, sizeof(out), "%.1f kHz", sampleRate / 1000.0f);
    }
    return out;
}

void drawText(vita2d_pgf* font, int x, int y, float scale, unsigned int color, const std::string& text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
}

void drawTextureFit(vita2d_texture* texture, float x, float y, float w, float h) {
    if (!texture) return;
    const float tw = static_cast<float>(vita2d_texture_get_width(texture));
    const float th = static_cast<float>(vita2d_texture_get_height(texture));
    if (tw <= 0.0f || th <= 0.0f) return;

    const float scale = std::min(w / tw, h / th);
    const float dw = tw * scale;
    const float dh = th * scale;
    vita2d_draw_texture_scale(texture, x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, scale, scale);
}

void drawCoverPlaceholder(vita2d_pgf* font, float x, float y, float w, float h) {
    vita2d_draw_rectangle(x, y, w, h, ACCENT_SOFT);
    vita2d_draw_rectangle(x + 8, y + 8, w - 16, h - 16, PANEL_2);
    drawText(font, static_cast<int>(x + w * 0.36f), static_cast<int>(y + h * 0.56f),
             1.45f, ACCENT, "♪");
}

void drawProgressBar(const AudioPlayer& player, int x, int y, int w, int h) {
    vita2d_draw_rectangle(x, y, w, h, rgba(65, 67, 78));

    const int duration = player.durationMs();
    const int position = player.positionMs();
    if (duration > 0) {
        float progress = static_cast<float>(position) / static_cast<float>(duration);
        progress = std::max(0.0f, std::min(1.0f, progress));
        vita2d_draw_rectangle(x, y, static_cast<float>(w) * progress, h, ACCENT);
    }
}

void drawMiniPlayer(vita2d_pgf* font, const std::string& selectedSong,
                    const TrackMetadata& metadata, const CoverArt& cover,
                    const AudioPlayer& player) {
    vita2d_draw_rectangle(0, 472, SCREEN_W, 72, PANEL);

    if (cover.hasTexture()) {
        vita2d_draw_rectangle(20, 486, 45, 45, PANEL_2);
        drawTextureFit(cover.texture(), 20, 486, 45, 45);
    } else {
        vita2d_draw_rectangle(20, 486, 45, 45, ACCENT_SOFT);
        drawText(font, 34, 516, 0.78f, ACCENT,
                 player.isPlaying() ? ">" : (player.isPaused() ? "||" : "*"));
    }

    if (selectedSong.empty()) {
        drawText(font, 82, 504, 0.76f, TEXT, "Ninguna cancion seleccionada");
        drawText(font, 82, 526, 0.58f, MUTED,
                 "X: reproducir  |  O: atras  |  △: ordenar  |  START: salir");
        return;
    }

    const std::string title = metadata.title.empty() ? filenameFromPath(selectedSong) : metadata.title;
    drawText(font, 82, 498, 0.76f, TEXT, shorten(title, 46));

    if (player.state() == AudioPlayer::State::Error) {
        drawText(font, 82, 522, 0.58f, ERROR_COLOR, shorten(player.lastError(), 64));
    } else {
        std::string details;
        if (!metadata.artist.empty()) details += metadata.artist;
        if (!metadata.album.empty() && metadata.album != "Album desconocido") {
            if (!details.empty()) details += "  •  ";
            details += metadata.album;
        }
        if (details.empty()) details = player.stateLabel();
        drawText(font, 82, 522, 0.58f, MUTED, shorten(details, 60));
    }

    drawProgressBar(player, 620, 508, 270, 5);
    drawText(font, 620, 531, 0.55f, MUTED,
             formatTime(player.positionMs()) + " / " + formatTime(player.durationMs()));
    drawText(font, 741, 531, 0.50f, MUTED, "SELECT: Ahora suena");
}

void drawLibrary(vita2d_pgf* font, const MusicBrowser& browser,
                 const std::string& selectedSong, const TrackMetadata& metadata,
                 const CoverArt& cover, const AudioPlayer& player) {
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

    drawMiniPlayer(font, selectedSong, metadata, cover, player);

    drawText(font, 746, 39, 0.65f, MUTED, browser.ascending() ? "A-Z" : "Z-A");
    drawText(font, 811, 39, 0.65f, ACCENT, "v0.3");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawNowPlaying(vita2d_pgf* font, const TrackMetadata& metadata,
                    const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    vita2d_draw_rectangle(0, 0, SCREEN_W, 72, PANEL);
    drawText(font, 28, 39, 1.18f, TEXT, "Ahora suena");
    drawText(font, 28, 62, 0.62f, MUTED, "O: volver a biblioteca");
    drawText(font, 823, 39, 0.65f, ACCENT, "v0.3");

    constexpr float coverX = 46.0f;
    constexpr float coverY = 104.0f;
    constexpr float coverSize = 316.0f;

    vita2d_draw_rectangle(coverX - 4, coverY - 4, coverSize + 8, coverSize + 8, PANEL_2);
    if (cover.hasTexture()) {
        drawTextureFit(cover.texture(), coverX, coverY, coverSize, coverSize);
    } else {
        drawCoverPlaceholder(font, coverX, coverY, coverSize, coverSize);
    }

    const int infoX = 408;
    drawText(font, infoX, 132, 1.12f, TEXT, shorten(metadata.title, 38));
    drawText(font, infoX, 168, 0.84f, ACCENT, shorten(metadata.artist, 44));
    drawText(font, infoX, 197, 0.68f, MUTED, shorten(metadata.album, 50));

    int y = 247;
    drawText(font, infoX, y, 0.62f, MUTED, "Formato");
    drawText(font, infoX + 112, y, 0.68f, TEXT, metadata.formatName);
    y += 30;

    drawText(font, infoX, y, 0.62f, MUTED, "Calidad");
    std::string quality = formatKhz(metadata.sampleRate);
    if (metadata.bitDepth > 0) {
        if (!quality.empty()) quality += "  •  ";
        quality += std::to_string(metadata.bitDepth) + "-bit";
    }
    if (metadata.channels > 0) {
        if (!quality.empty()) quality += "  •  ";
        quality += metadata.channels == 1 ? "Mono" : "Estereo";
    }
    drawText(font, infoX + 112, y, 0.68f, TEXT, quality.empty() ? "-" : quality);
    y += 30;

    drawText(font, infoX, y, 0.62f, MUTED, "Bitrate");
    drawText(font, infoX + 112, y, 0.68f, TEXT,
             metadata.bitrateKbps > 0 ? std::to_string(metadata.bitrateKbps) + " kbps" : "-");
    y += 30;

    drawText(font, infoX, y, 0.62f, MUTED, "Genero");
    drawText(font, infoX + 112, y, 0.68f, TEXT, metadata.genre.empty() ? "-" : shorten(metadata.genre, 30));
    y += 30;

    drawText(font, infoX, y, 0.62f, MUTED, "Ano");
    drawText(font, infoX + 112, y, 0.68f, TEXT, metadata.date.empty() ? "-" : shorten(metadata.date, 18));

    if (!cover.sourceLabel().empty()) {
        drawText(font, 46, 443, 0.54f, MUTED, "Caratula: " + shorten(cover.sourceLabel(), 38));
    }

    drawProgressBar(player, 46, 476, 868, 7);
    drawText(font, 46, 505, 0.61f, MUTED, formatTime(player.positionMs()));
    drawText(font, 871, 505, 0.61f, MUTED, formatTime(player.durationMs()));

    const std::string state = player.isPaused() ? "PAUSADO" :
                              (player.isPlaying() ? "REPRODUCIENDO" : player.stateLabel());
    drawText(font, 408, 438, 0.62f, ACCENT, state);
    drawText(font, 306, 531, 0.58f, MUTED,
             "□ / X pausa   ←/→ -5/+5s   L/R anterior/siguiente");

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
    TrackMetadata metadata;
    CoverArt cover;
    std::string selectedSong;
    bool nowPlaying = false;

    // Snapshot of the folder that started playback. Keeping this independent from
    // the file browser means the user can keep browsing other folders while the
    // current album/folder continues in the expected order.
    std::vector<std::string> playbackQueue;
    int playbackIndex = -1;

    auto startTrack = [&](const std::string& path) {
        if (player.playFile(path)) {
            selectedSong = path;
            // Audio starts on its own thread first, so metadata/art loading does not
            // add the old multi-second delay before playback begins.
            metadata = MetadataReader::read(path);
            cover.loadForTrack(path);
        }
    };

    auto rebuildPlaybackQueue = [&](const std::string& activePath) {
        playbackQueue.clear();
        playbackIndex = -1;

        const auto& entries = browser.entries();
        for (const auto& entry : entries) {
            if (!entry.isDirectory) {
                if (entry.path == activePath) {
                    playbackIndex = static_cast<int>(playbackQueue.size());
                }
                playbackQueue.push_back(entry.path);
            }
        }
    };

    auto playQueueIndex = [&](int index) -> bool {
        if (index < 0 || index >= static_cast<int>(playbackQueue.size())) return false;
        playbackIndex = index;
        startTrack(playbackQueue[playbackIndex]);
        return true;
    };

    auto playAdjacentFromQueue = [&](int direction, bool wrap) -> bool {
        if (playbackQueue.empty() || playbackIndex < 0 || direction == 0) return false;

        int next = playbackIndex + direction;
        if (wrap) {
            const int count = static_cast<int>(playbackQueue.size());
            while (next < 0) next += count;
            next %= count;
        } else if (next < 0 || next >= static_cast<int>(playbackQueue.size())) {
            return false;
        }

        return playQueueIndex(next);
    };

    SceCtrlData pad{};
    unsigned int previousButtons = 0;
    bool running = true;

    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const unsigned int pressed = pad.buttons & ~previousButtons;
        previousButtons = pad.buttons;

        if (!nowPlaying) {
            if (pressed & SCE_CTRL_UP) browser.moveUp();
            if (pressed & SCE_CTRL_DOWN) browser.moveDown();
        }

        if (pressed & SCE_CTRL_CROSS) {
            if (nowPlaying) {
                player.togglePause();
            } else {
                std::string previousSelection = selectedSong;
                if (browser.enterSelected(selectedSong)) {
                    const bool sameActiveTrack =
                        selectedSong == player.currentPath() &&
                        (player.isPlaying() || player.isPaused());

                    if (sameActiveTrack) {
                        player.togglePause();
                    } else {
                        // A manual song selection defines a new playback context:
                        // every audio file in the current folder, in the visible order.
                        rebuildPlaybackQueue(selectedSong);
                        if (playbackIndex >= 0) {
                            playQueueIndex(playbackIndex);
                        } else {
                            startTrack(selectedSong);
                        }
                    }
                } else if (selectedSong != previousSelection) {
                    // Entering a directory leaves current playback untouched.
                }
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
            if (!playAdjacentFromQueue(-1, true)) {
                // Fallback for a track started before a queue could be captured.
                if (browser.selectAdjacentAudio(-1, selectedSong)) {
                    rebuildPlaybackQueue(selectedSong);
                    startTrack(selectedSong);
                }
            }
        }

        if (pressed & SCE_CTRL_RTRIGGER) {
            if (!playAdjacentFromQueue(1, true)) {
                if (browser.selectAdjacentAudio(1, selectedSong)) {
                    rebuildPlaybackQueue(selectedSong);
                    startTrack(selectedSong);
                }
            }
        }

        if (pressed & SCE_CTRL_CIRCLE) {
            if (nowPlaying) {
                nowPlaying = false;
            } else {
                browser.goBack();
            }
        }

        if (!nowPlaying && (pressed & SCE_CTRL_TRIANGLE)) {
            browser.toggleSortDirection();
        }

        if ((pressed & SCE_CTRL_SELECT) && !selectedSong.empty()) {
            nowPlaying = !nowPlaying;
        }

        if (pressed & SCE_CTRL_START) {
            running = false;
        }

        // Automatic continuous playback: only advance on a natural EOF event.
        // The last track stops normally; repeat-all will be a later playback mode.
        if (player.consumeTrackFinished()) {
            playAdjacentFromQueue(1, false);
        }

        if (nowPlaying && !selectedSong.empty()) {
            drawNowPlaying(font, metadata, cover, player);
        } else {
            drawLibrary(font, browser, selectedSong, metadata, cover, player);
        }
        sceKernelDelayThread(16000);
    }

    player.stop();
    cover.clear();
    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
