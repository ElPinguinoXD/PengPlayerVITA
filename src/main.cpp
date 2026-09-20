#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "browser.hpp"
#include "audio/audio_player.hpp"
#include "media/metadata.hpp"
#include "media/cover_art.hpp"
#include "library/library_manager.hpp"

namespace {
constexpr int SCREEN_W = 960;
constexpr int SCREEN_H = 544;
constexpr int ROW_H = 35;
constexpr int VISIBLE_ROWS = 9;

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

std::string folderFromPath(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash <= 4) return path.substr(0, slash + 1);
    return path.substr(0, slash);
}

std::string basenameFromPath(const std::string& path) {
    std::string clean = path;
    while (clean.size() > 5 && clean.back() == '/') clean.pop_back();
    const std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos) return clean;
    if (slash + 1 >= clean.size()) return clean;
    return clean.substr(slash + 1);
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
    if (sampleRate % 1000 == 0) std::snprintf(out, sizeof(out), "%d kHz", sampleRate / 1000);
    else std::snprintf(out, sizeof(out), "%.1f kHz", sampleRate / 1000.0f);
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
    drawText(font, static_cast<int>(x + w * 0.36f), static_cast<int>(y + h * 0.56f), 1.45f, ACCENT, "♪");
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

struct GroupItem {
    std::string label;
    std::string key;
    int count = 0;
};

enum class Screen {
    Library,
    GroupTracks,
    NowPlaying,
    Settings,
    Roots,
    Mounts,
    FolderPicker
};

enum class LibraryTab {
    Songs = 0,
    Artists = 1,
    Albums = 2,
    Folders = 3
};

std::vector<const TrackMetadata*> sortedSongs(const LibraryManager& library) {
    std::vector<const TrackMetadata*> result;
    result.reserve(library.tracks().size());
    for (const auto& track : library.tracks()) result.push_back(&track);
    std::sort(result.begin(), result.end(), [](const TrackMetadata* a, const TrackMetadata* b) {
        if (a->title != b->title) return a->title < b->title;
        if (a->artist != b->artist) return a->artist < b->artist;
        return a->path < b->path;
    });
    return result;
}

std::vector<GroupItem> buildGroups(const LibraryManager& library, LibraryTab tab) {
    std::vector<GroupItem> result;

    auto keyFor = [tab](const TrackMetadata& track) -> std::string {
        if (tab == LibraryTab::Artists) return track.artist.empty() ? "Artista desconocido" : track.artist;
        if (tab == LibraryTab::Albums) return track.album.empty() ? "Album desconocido" : track.album;
        return folderFromPath(track.path);
    };

    for (const auto& track : library.tracks()) {
        const std::string key = keyFor(track);
        auto found = std::find_if(result.begin(), result.end(), [&](const GroupItem& item) {
            return item.key == key;
        });
        if (found == result.end()) {
            GroupItem item;
            item.key = key;
            item.label = tab == LibraryTab::Folders ? basenameFromPath(key) : key;
            item.count = 1;
            result.push_back(item);
        } else {
            ++found->count;
        }
    }

    std::sort(result.begin(), result.end(), [](const GroupItem& a, const GroupItem& b) {
        if (a.label != b.label) return a.label < b.label;
        return a.key < b.key;
    });
    return result;
}

std::vector<const TrackMetadata*> tracksForGroup(const LibraryManager& library, LibraryTab tab,
                                                  const std::string& key) {
    std::vector<const TrackMetadata*> result;
    for (const auto& track : library.tracks()) {
        bool match = false;
        if (tab == LibraryTab::Artists) match = track.artist == key;
        else if (tab == LibraryTab::Albums) match = track.album == key;
        else if (tab == LibraryTab::Folders) match = folderFromPath(track.path) == key;
        if (match) result.push_back(&track);
    }
    std::sort(result.begin(), result.end(), [](const TrackMetadata* a, const TrackMetadata* b) {
        if (a->trackNumber != b->trackNumber && !a->trackNumber.empty() && !b->trackNumber.empty())
            return a->trackNumber < b->trackNumber;
        if (a->title != b->title) return a->title < b->title;
        return a->path < b->path;
    });
    return result;
}

void clampList(int count, int visibleRows, int& selected, int& scroll) {
    if (count <= 0) {
        selected = 0;
        scroll = 0;
        return;
    }
    if (selected < 0) selected = count - 1;
    if (selected >= count) selected = 0;
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + visibleRows) scroll = selected - visibleRows + 1;
    if (scroll < 0) scroll = 0;
}

void moveList(int direction, int count, int visibleRows, int& selected, int& scroll) {
    if (count <= 0) return;
    selected += direction;
    clampList(count, visibleRows, selected, scroll);
}


void moveSimple(int direction, int count, int& selected) {
    if (count <= 0) return;
    selected += direction;
    if (selected < 0) selected = count - 1;
    if (selected >= count) selected = 0;
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
        drawText(font, 82, 526, 0.56f, MUTED, "X: abrir/reproducir  |  △: seccion  |  START: ajustes");
        return;
    }

    const std::string title = metadata.title.empty() ? filenameFromPath(selectedSong) : metadata.title;
    drawText(font, 82, 498, 0.76f, TEXT, shorten(title, 44));

    std::string details;
    if (!metadata.artist.empty()) details += metadata.artist;
    if (!metadata.album.empty() && metadata.album != "Album desconocido") {
        if (!details.empty()) details += "  •  ";
        details += metadata.album;
    }
    if (details.empty()) details = player.stateLabel();
    drawText(font, 82, 522, 0.56f, player.state() == AudioPlayer::State::Error ? ERROR_COLOR : MUTED,
             player.state() == AudioPlayer::State::Error ? shorten(player.lastError(), 58) : shorten(details, 58));

    drawProgressBar(player, 620, 508, 270, 5);
    drawText(font, 620, 531, 0.53f, MUTED,
             formatTime(player.positionMs()) + " / " + formatTime(player.durationMs()));
    drawText(font, 777, 531, 0.49f, MUTED, "SELECT: Ahora suena");
}

void drawHeader(vita2d_pgf* font, const std::string& title, const std::string& subtitle) {
    vita2d_draw_rectangle(0, 0, SCREEN_W, 72, PANEL);
    drawText(font, 28, 39, 1.18f, TEXT, title);
    drawText(font, 28, 62, 0.62f, MUTED, subtitle);
    drawText(font, 834, 39, 0.65f, ACCENT, "v0.4");
}

void drawLibrary(vita2d_pgf* font, const LibraryManager& library, LibraryTab tab,
                 int selected, int scroll, const std::string& selectedSong,
                 const TrackMetadata& metadata, const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "PengPlayer", "Biblioteca  |  △ cambiar seccion  |  START ajustes");

    const char* tabs[4] = {"Canciones", "Artistas", "Albumes", "Carpetas"};
    for (int i = 0; i < 4; ++i) {
        const int x = 20 + i * 230;
        const bool active = static_cast<int>(tab) == i;
        vita2d_draw_rectangle(x, 82, 218, 31, active ? ACCENT_SOFT : PANEL_2);
        if (active) vita2d_draw_rectangle(x, 110, 218, 3, ACCENT);
        drawText(font, x + 14, 104, 0.66f, active ? TEXT : MUTED, tabs[i]);
    }

    if (library.isScanning()) {
        const int total = library.scanTotal();
        const int done = library.scanProcessed();
        const std::string scan = "Escaneando biblioteca: " + std::to_string(done) + " / " + std::to_string(total);
        drawText(font, 28, 137, 0.56f, ACCENT, scan);
    } else {
        drawText(font, 28, 137, 0.56f, MUTED,
                 std::to_string(library.tracks().size()) + " canciones  |  " + shorten(library.statusMessage(), 60));
    }

    const int listTop = 150;
    if (tab == LibraryTab::Songs) {
        const auto songs = sortedSongs(library);
        const int end = std::min(scroll + VISIBLE_ROWS, static_cast<int>(songs.size()));
        if (songs.empty()) drawText(font, 30, 188, 0.74f, MUTED, "La biblioteca esta vacia. Usa START > Actualizar biblioteca.");
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            drawText(font, 38, y + 17, 0.71f, TEXT, shorten(songs[i]->title, 52));
            std::string detail = songs[i]->artist;
            if (!songs[i]->album.empty() && songs[i]->album != "Album desconocido") detail += "  •  " + songs[i]->album;
            drawText(font, 520, y + 17, 0.54f, MUTED, shorten(detail, 47));
        }
    } else {
        const auto groups = buildGroups(library, tab);
        const int end = std::min(scroll + VISIBLE_ROWS, static_cast<int>(groups.size()));
        if (groups.empty()) drawText(font, 30, 188, 0.74f, MUTED, "No hay elementos para mostrar.");
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            drawText(font, 38, y + 17, 0.71f, TEXT, shorten(groups[i].label, 54));
            if (tab == LibraryTab::Folders) {
                drawText(font, 470, y + 17, 0.51f, MUTED, shorten(groups[i].key, 47));
            } else {
                drawText(font, 790, y + 17, 0.54f, MUTED,
                         std::to_string(groups[i].count) + (groups[i].count == 1 ? " cancion" : " canciones"));
            }
        }
    }

    drawMiniPlayer(font, selectedSong, metadata, cover, player);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawGroupTracks(vita2d_pgf* font, const std::string& groupLabel,
                     const std::vector<const TrackMetadata*>& songs, int selected, int scroll,
                     const std::string& selectedSong, const TrackMetadata& metadata,
                     const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, shorten(groupLabel, 42), "O: volver  |  X: reproducir");

    const int listTop = 92;
    const int visible = 10;
    const int end = std::min(scroll + visible, static_cast<int>(songs.size()));
    for (int i = scroll; i < end; ++i) {
        const int y = listTop + (i - scroll) * 36;
        if (i == selected) {
            vita2d_draw_rectangle(20, y - 2, 920, 34, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 34, ACCENT);
        }
        drawText(font, 38, y + 18, 0.72f, TEXT, shorten(songs[i]->title, 56));
        drawText(font, 590, y + 18, 0.53f, MUTED, shorten(songs[i]->artist, 28));
    }
    drawMiniPlayer(font, selectedSong, metadata, cover, player);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawNowPlaying(vita2d_pgf* font, const TrackMetadata& metadata,
                    const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Ahora suena", "O: volver a biblioteca");

    constexpr float coverX = 46.0f;
    constexpr float coverY = 104.0f;
    constexpr float coverSize = 316.0f;
    vita2d_draw_rectangle(coverX - 4, coverY - 4, coverSize + 8, coverSize + 8, PANEL_2);
    if (cover.hasTexture()) drawTextureFit(cover.texture(), coverX, coverY, coverSize, coverSize);
    else drawCoverPlaceholder(font, coverX, coverY, coverSize, coverSize);

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

    if (!cover.sourceLabel().empty()) drawText(font, 46, 443, 0.54f, MUTED, "Caratula: " + shorten(cover.sourceLabel(), 38));
    drawProgressBar(player, 46, 476, 868, 7);
    drawText(font, 46, 505, 0.61f, MUTED, formatTime(player.positionMs()));
    drawText(font, 871, 505, 0.61f, MUTED, formatTime(player.durationMs()));
    const std::string state = player.isPaused() ? "PAUSADO" : (player.isPlaying() ? "REPRODUCIENDO" : player.stateLabel());
    drawText(font, 408, 438, 0.62f, ACCENT, state);
    drawText(font, 306, 531, 0.58f, MUTED, "□ / X pausa   ←/→ -5/+5s   L/R anterior/siguiente");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawMenu(vita2d_pgf* font, const std::string& title, const std::string& subtitle,
              const std::vector<std::string>& items, int selected, const std::string& footer) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, title, subtitle);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const int y = 112 + i * 54;
        if (i == selected) {
            vita2d_draw_rectangle(26, y - 25, 908, 44, ACCENT_SOFT);
            vita2d_draw_rectangle(26, y - 25, 5, 44, ACCENT);
        }
        drawText(font, 48, y, 0.78f, TEXT, shorten(items[i], 82));
    }
    drawText(font, 28, 518, 0.57f, MUTED, footer);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawFolderPicker(vita2d_pgf* font, const MusicBrowser& browser) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Seleccionar carpeta", "X: abrir carpeta  |  △: usar esta carpeta  |  O: atras");
    vita2d_draw_rectangle(20, 82, 920, 28, PANEL_2);
    drawText(font, 32, 103, 0.62f, MUTED, shorten(browser.currentPath(), 92));

    const auto& entries = browser.entries();
    const int start = browser.scrollOffset();
    const int end = std::min(start + 11, static_cast<int>(entries.size()));
    if (!browser.lastError().empty()) drawText(font, 32, 150, 0.68f, ERROR_COLOR, browser.lastError());
    for (int i = start; i < end; ++i) {
        const int y = 126 + (i - start) * 31;
        if (i == browser.selectedIndex()) {
            vita2d_draw_rectangle(20, y - 2, 920, 29, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 29, ACCENT);
        }
        drawText(font, 38, y + 19, 0.67f, entries[i].isDirectory ? ACCENT : MUTED,
                 entries[i].isDirectory ? "[DIR]" : "[AUDIO]");
        drawText(font, 126, y + 19, 0.70f, TEXT, shorten(entries[i].name, 70));
    }
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

    LibraryManager library;
    library.initialize();

    AudioPlayer player;
    TrackMetadata metadata;
    CoverArt cover;
    std::string selectedSong;

    Screen screen = Screen::Library;
    Screen returnScreen = Screen::Library;
    LibraryTab tab = LibraryTab::Songs;
    LibraryTab groupTab = LibraryTab::Artists;
    std::string groupKey;
    std::string groupLabel;

    int selected = 0;
    int scroll = 0;
    int groupSelected = 0;
    int groupScroll = 0;
    int settingsSelected = 0;
    int rootsSelected = 0;
    int mountsSelected = 0;

    std::unique_ptr<MusicBrowser> folderPicker;
    std::vector<std::string> playbackQueue;
    int playbackIndex = -1;

    auto startTrack = [&](const std::string& path) {
        if (!player.playFile(path)) return;
        selectedSong = path;
        const TrackMetadata* cached = library.findTrack(path);
        metadata = cached ? *cached : MetadataReader::read(path);
        cover.loadForTrack(path);
    };

    auto setQueue = [&](const std::vector<const TrackMetadata*>& tracks, int index) {
        playbackQueue.clear();
        for (const auto* track : tracks) playbackQueue.push_back(track->path);
        playbackIndex = index;
        if (index >= 0 && index < static_cast<int>(playbackQueue.size())) startTrack(playbackQueue[index]);
    };

    auto playQueueIndex = [&](int index) -> bool {
        if (index < 0 || index >= static_cast<int>(playbackQueue.size())) return false;
        playbackIndex = index;
        startTrack(playbackQueue[index]);
        return true;
    };

    auto playAdjacent = [&](int direction, bool wrap) -> bool {
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
        library.updateScan(1);

        sceCtrlPeekBufferPositive(0, &pad, 1);
        const unsigned int pressed = pad.buttons & ~previousButtons;
        previousButtons = pad.buttons;

        if (screen == Screen::Library) {
            const int count = tab == LibraryTab::Songs
                ? static_cast<int>(sortedSongs(library).size())
                : static_cast<int>(buildGroups(library, tab).size());
            clampList(count, VISIBLE_ROWS, selected, scroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, count, VISIBLE_ROWS, selected, scroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, count, VISIBLE_ROWS, selected, scroll);

            if (pressed & SCE_CTRL_TRIANGLE) {
                tab = static_cast<LibraryTab>((static_cast<int>(tab) + 1) % 4);
                selected = 0;
                scroll = 0;
            }

            if (pressed & SCE_CTRL_CROSS) {
                if (tab == LibraryTab::Songs) {
                    const auto songs = sortedSongs(library);
                    if (selected >= 0 && selected < static_cast<int>(songs.size())) setQueue(songs, selected);
                } else {
                    const auto groups = buildGroups(library, tab);
                    if (selected >= 0 && selected < static_cast<int>(groups.size())) {
                        groupTab = tab;
                        groupKey = groups[selected].key;
                        groupLabel = groups[selected].label;
                        groupSelected = 0;
                        groupScroll = 0;
                        screen = Screen::GroupTracks;
                    }
                }
            }

            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                screen = Screen::Settings;
            }
        } else if (screen == Screen::GroupTracks) {
            const auto songs = tracksForGroup(library, groupTab, groupKey);
            clampList(static_cast<int>(songs.size()), 10, groupSelected, groupScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, static_cast<int>(songs.size()), 10, groupSelected, groupScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, static_cast<int>(songs.size()), 10, groupSelected, groupScroll);
            if ((pressed & SCE_CTRL_CROSS) && groupSelected < static_cast<int>(songs.size())) setQueue(songs, groupSelected);
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Library;
        } else if (screen == Screen::NowPlaying) {
            if (pressed & SCE_CTRL_CROSS) player.togglePause();
            if (pressed & SCE_CTRL_CIRCLE) screen = returnScreen;
        } else if (screen == Screen::Settings) {
            const int count = 3;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, settingsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, settingsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                if (settingsSelected == 0) {
                    library.startScan();
                    screen = Screen::Library;
                    selected = 0;
                    scroll = 0;
                } else if (settingsSelected == 1) {
                    rootsSelected = 0;
                    screen = Screen::Roots;
                } else {
                    running = false;
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Library;
        } else if (screen == Screen::Roots) {
            const int count = static_cast<int>(library.roots().size()) + 1;
            clampList(count, 8, rootsSelected, scroll);
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, rootsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, rootsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                if (rootsSelected == static_cast<int>(library.roots().size())) {
                    mountsSelected = 0;
                    screen = Screen::Mounts;
                }
            }
            if ((pressed & SCE_CTRL_SQUARE) && rootsSelected < static_cast<int>(library.roots().size())) {
                if (library.removeRoot(static_cast<std::size_t>(rootsSelected))) {
                    library.startScan();
                    if (rootsSelected >= static_cast<int>(library.roots().size())) rootsSelected = static_cast<int>(library.roots().size()) - 1;
                    if (rootsSelected < 0) rootsSelected = 0;
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Settings;
        } else if (screen == Screen::Mounts) {
            const int count = 3;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, mountsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, mountsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                static const char* mounts[] = {"ux0:/", "uma0:/", "imc0:/"};
                folderPicker.reset(new MusicBrowser(mounts[mountsSelected]));
                screen = Screen::FolderPicker;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Roots;
        } else if (screen == Screen::FolderPicker && folderPicker) {
            if (pressed & SCE_CTRL_UP) folderPicker->moveUp();
            if (pressed & SCE_CTRL_DOWN) folderPicker->moveDown();
            if (pressed & SCE_CTRL_CROSS) {
                std::string ignored;
                folderPicker->enterSelected(ignored);
            }
            if (pressed & SCE_CTRL_TRIANGLE) {
                if (library.addRoot(folderPicker->currentPath())) {
                    library.startScan();
                    rootsSelected = static_cast<int>(library.roots().size()) - 1;
                    screen = Screen::Roots;
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) {
                if (!folderPicker->goBack()) screen = Screen::Mounts;
            }
        }

        const bool playbackControls = screen == Screen::Library || screen == Screen::GroupTracks || screen == Screen::NowPlaying;
        if (playbackControls) {
            if ((pressed & SCE_CTRL_SQUARE) && screen != Screen::NowPlaying) player.togglePause();
            if (pressed & SCE_CTRL_LEFT) player.seekRelative(-5);
            if (pressed & SCE_CTRL_RIGHT) player.seekRelative(5);
            if (pressed & SCE_CTRL_LTRIGGER) playAdjacent(-1, true);
            if (pressed & SCE_CTRL_RTRIGGER) playAdjacent(1, true);

            if ((pressed & SCE_CTRL_SELECT) && !selectedSong.empty()) {
                if (screen == Screen::NowPlaying) screen = returnScreen;
                else {
                    returnScreen = screen;
                    screen = Screen::NowPlaying;
                }
            }
        }

        if (player.consumeTrackFinished()) playAdjacent(1, false);

        if (screen == Screen::Library) {
            drawLibrary(font, library, tab, selected, scroll, selectedSong, metadata, cover, player);
        } else if (screen == Screen::GroupTracks) {
            const auto songs = tracksForGroup(library, groupTab, groupKey);
            drawGroupTracks(font, groupLabel, songs, groupSelected, groupScroll, selectedSong, metadata, cover, player);
        } else if (screen == Screen::NowPlaying) {
            drawNowPlaying(font, metadata, cover, player);
        } else if (screen == Screen::Settings) {
            std::vector<std::string> items = {"Actualizar biblioteca", "Rutas de musica", "Salir de PengPlayer"};
            drawMenu(font, "Ajustes", "Biblioteca y aplicacion", items, settingsSelected,
                     "X: seleccionar  |  O: volver");
        } else if (screen == Screen::Roots) {
            std::vector<std::string> items = library.roots();
            items.push_back("+ Anadir ruta de musica");
            drawMenu(font, "Rutas de musica", "PengPlayer escanea todas estas carpetas", items, rootsSelected,
                     "X: anadir  |  □: eliminar ruta  |  O: volver");
        } else if (screen == Screen::Mounts) {
            std::vector<std::string> items = {"ux0:/  Memoria principal / SD2Vita", "uma0:/  Almacenamiento secundario", "imc0:/  Memoria interna"};
            drawMenu(font, "Seleccionar almacenamiento", "Elige donde esta tu musica", items, mountsSelected,
                     "X: abrir  |  O: volver");
        } else if (screen == Screen::FolderPicker && folderPicker) {
            drawFolderPicker(font, *folderPicker);
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
