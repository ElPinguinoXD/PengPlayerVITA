#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "browser.hpp"
#include "image_browser.hpp"
#include "audio/audio_player.hpp"
#include "media/metadata.hpp"
#include "media/cover_art.hpp"
#include "library/library_manager.hpp"
#include "preferences/preferences_manager.hpp"
#include "visualizer/audio_visualizer.hpp"
#include "playlists/playlist_manager.hpp"
#include "smart/smart_library_manager.hpp"
#include "ui/text_input.hpp"

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
unsigned int ACCENT = rgba(153, 102, 255);
unsigned int ACCENT_SOFT = rgba(68, 47, 98);
float UI_SCALE = 1.0f;
const unsigned int ERROR_COLOR = rgba(255, 115, 115);

struct RgbColor {
    unsigned int r;
    unsigned int g;
    unsigned int b;
};

int normalizeHue(int hue) {
    hue %= 360;
    if (hue < 0) hue += 360;
    return hue;
}

RgbColor hueToRgb(int hue) {
    hue = normalizeHue(hue);
    const int sector = hue / 60;
    const int offset = hue % 60;
    const unsigned int rising = static_cast<unsigned int>((offset * 255) / 60);
    const unsigned int falling = 255U - rising;

    switch (sector) {
        case 0: return {255U, rising, 0U};
        case 1: return {falling, 255U, 0U};
        case 2: return {0U, 255U, rising};
        case 3: return {0U, falling, 255U};
        case 4: return {rising, 0U, 255U};
        default: return {255U, 0U, falling};
    }
}

void applyAccentHue(int hue) {
    const RgbColor color = hueToRgb(hue);
    ACCENT = rgba(color.r, color.g, color.b);
    ACCENT_SOFT = rgba(color.r * 35 / 100, color.g * 35 / 100, color.b * 35 / 100);
}

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
    vita2d_pgf_draw_text(font, x, y, color, scale * UI_SCALE, text.c_str());
}

void drawTextureSquareCrop(vita2d_texture* texture, float x, float y, float w, float h) {
    if (!texture) return;
    const float tw = static_cast<float>(vita2d_texture_get_width(texture));
    const float th = static_cast<float>(vita2d_texture_get_height(texture));
    if (tw <= 0.0f || th <= 0.0f || w <= 0.0f || h <= 0.0f) return;

    // Las caratulas siempre se muestran como un recorte centrado que llena
    // por completo el cuadro, en vez de dejar bandas vacias.
    const float sourceSide = std::min(tw, th);
    const float sourceX = (tw - sourceSide) * 0.5f;
    const float sourceY = (th - sourceSide) * 0.5f;
    vita2d_draw_texture_part_scale(
        texture, x, y, sourceX, sourceY, sourceSide, sourceSide,
        w / sourceSide, h / sourceSide);
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
    Appearance,
    SongOptions,
    Roots,
    Mounts,
    FolderPicker,
    CoverMounts,
    CoverPicker,
    Queue,
    Playlists,
    PlaylistTracks,
    PlaylistAddSongs,
    AddToPlaylist,
    CategoryTracks,
    CategoryAddSongs,
    AddToCategory,
    SearchInput,
    ConfirmDelete
};

enum class ConfirmAction {
    None,
    DeletePlaylist,
    RemovePlaylistTrack,
    RemoveMusicRoot,
    DeleteCategory,
    RemoveCategoryTrack
};

enum class LibraryTab {
    Songs = 0,
    Artists = 1,
    Albums = 2,
    Folders = 3,
    Playlists = 4,
    Favorites = 5,
    Categories = 6,
    Recent = 7,
    MostPlayed = 8,
    Search = 9
};

constexpr int kLibraryTabCount = 10;

const char* libraryTabName(LibraryTab tab) {
    switch (tab) {
        case LibraryTab::Songs: return "Canciones";
        case LibraryTab::Artists: return "Artistas";
        case LibraryTab::Albums: return "Albumes";
        case LibraryTab::Folders: return "Carpetas";
        case LibraryTab::Playlists: return "Playlists";
        case LibraryTab::Favorites: return "Favoritos";
        case LibraryTab::Categories: return "Categorias";
        case LibraryTab::Recent: return "Recientes";
        case LibraryTab::MostPlayed: return "Mas reproducidas";
        case LibraryTab::Search: return "Buscar";
        default: return "Biblioteca";
    }
}


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

std::vector<const TrackMetadata*> tracksFromPaths(const LibraryManager& library, const std::vector<std::string>& paths) {
    std::vector<const TrackMetadata*> result;
    for (const auto& path : paths) {
        const TrackMetadata* track = library.findTrack(path);
        if (track) result.push_back(track);
    }
    return result;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    });
    return value;
}

std::vector<const TrackMetadata*> searchTracks(const LibraryManager& library, const std::string& query) {
    std::vector<const TrackMetadata*> result;
    const std::string needle = lowerAscii(query);
    if (needle.empty()) return result;
    for (const auto& track : library.tracks()) {
        const std::string haystack = lowerAscii(track.title + "\n" + track.artist + "\n" + track.album + "\n" + track.genre + "\n" + track.path);
        if (haystack.find(needle) != std::string::npos) result.push_back(&track);
    }
    std::sort(result.begin(), result.end(), [](const TrackMetadata* a, const TrackMetadata* b) {
        if (a->title != b->title) return a->title < b->title;
        if (a->artist != b->artist) return a->artist < b->artist;
        return a->path < b->path;
    });
    return result;
}

std::vector<const TrackMetadata*> tracksForSmartTab(const LibraryManager& library,
                                                      const SmartLibraryManager& smart,
                                                      LibraryTab tab,
                                                      const std::string& searchQuery) {
    if (tab == LibraryTab::Favorites) {
        auto result = tracksFromPaths(library, smart.favoritePaths());
        std::sort(result.begin(), result.end(), [](const TrackMetadata* a, const TrackMetadata* b) {
            if (a->title != b->title) return a->title < b->title;
            return a->path < b->path;
        });
        return result;
    }
    if (tab == LibraryTab::Recent) return tracksFromPaths(library, smart.recentPaths());
    if (tab == LibraryTab::MostPlayed) return tracksFromPaths(library, smart.mostPlayedPaths());
    if (tab == LibraryTab::Search) return searchTracks(library, searchQuery);
    return {};
}

const char* repeatModeName(int mode) {
    if (mode == 1) return "Todo";
    if (mode == 2) return "Una";
    return "Off";
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
        drawTextureSquareCrop(cover.texture(), 20, 486, 45, 45);
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
    drawText(font, 834, 39, 0.65f, ACCENT, "v0.8");
}

void drawLibrary(vita2d_pgf* font, const LibraryManager& library, const PlaylistManager& playlists,
                 const SmartLibraryManager& smart, LibraryTab tab, int selected, int scroll,
                 const std::string& searchQuery, const std::string& selectedSong,
                 const TrackMetadata& metadata, const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "PengPlayer", "Biblioteca  |  △ cambiar seccion  |  START ajustes");

    const int tabIndex = static_cast<int>(tab);
    const LibraryTab prevTab = static_cast<LibraryTab>((tabIndex + kLibraryTabCount - 1) % kLibraryTabCount);
    const LibraryTab nextTab = static_cast<LibraryTab>((tabIndex + 1) % kLibraryTabCount);
    vita2d_draw_rectangle(20, 82, 250, 31, PANEL_2);
    vita2d_draw_rectangle(280, 82, 400, 31, ACCENT_SOFT);
    vita2d_draw_rectangle(690, 82, 250, 31, PANEL_2);
    vita2d_draw_rectangle(280, 110, 400, 3, ACCENT);
    drawText(font, 34, 104, 0.54f, MUTED, std::string("< ") + libraryTabName(prevTab));
    drawText(font, 414, 104, 0.66f, TEXT, libraryTabName(tab));
    drawText(font, 735, 104, 0.54f, MUTED, std::string(libraryTabName(nextTab)) + " >");

    if (library.isScanning()) {
        const int total = library.scanTotal();
        const int done = library.scanProcessed();
        drawText(font, 28, 137, 0.56f, ACCENT,
                 "Escaneando biblioteca: " + std::to_string(done) + " / " + std::to_string(total));
    } else {
        std::string line = std::to_string(library.tracks().size()) + " canciones";
        if (tab == LibraryTab::Search && !searchQuery.empty()) line += "  |  Busqueda: " + shorten(searchQuery, 45);
        else line += "  |  " + shorten(library.statusMessage(), 58);
        drawText(font, 28, 137, 0.56f, MUTED, line);
    }

    const int listTop = 150;
    const bool smartTrackTab = tab == LibraryTab::Favorites || tab == LibraryTab::Recent ||
                               tab == LibraryTab::MostPlayed;

    if (tab == LibraryTab::Songs || smartTrackTab) {
        std::vector<const TrackMetadata*> songs = tab == LibraryTab::Songs
            ? sortedSongs(library)
            : tracksForSmartTab(library, smart, tab, searchQuery);
        const int end = std::min(scroll + VISIBLE_ROWS, static_cast<int>(songs.size()));
        if (songs.empty()) {
            std::string empty = "No hay canciones para mostrar.";
            if (tab == LibraryTab::Favorites) empty = "Aun no tienes favoritos. Marcalos desde Opciones de cancion.";
            else if (tab == LibraryTab::Recent) empty = "Todavia no has reproducido canciones en PengPlayer.";
            else if (tab == LibraryTab::MostPlayed) empty = "Las canciones mas reproducidas apareceran aqui.";
            drawText(font, 30, 188, 0.70f, MUTED, empty);
        }
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            std::string prefix = smart.isFavorite(songs[i]->path) ? "[F] " : "";
            drawText(font, 38, y + 17, 0.69f, TEXT, shorten(prefix + songs[i]->title, 51));
            std::string detail = songs[i]->artist;
            if (tab == LibraryTab::MostPlayed) detail = std::to_string(smart.playCount(songs[i]->path)) + " reproducciones  |  " + detail;
            drawText(font, 550, y + 17, 0.52f, MUTED, shorten(detail, 42));
        }
    } else if (tab == LibraryTab::Search) {
        const auto songs = searchTracks(library, searchQuery);
        const int total = static_cast<int>(songs.size()) + 1;
        const int end = std::min(scroll + VISIBLE_ROWS, total);
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            if (i == 0) {
                drawText(font, 38, y + 17, 0.70f, ACCENT,
                         searchQuery.empty() ? "+ Escribir busqueda" : "Buscar de nuevo: " + shorten(searchQuery, 55));
                drawText(font, 795, y + 17, 0.50f, MUTED, "X teclado");
            } else {
                const TrackMetadata* track = songs[i - 1];
                drawText(font, 38, y + 17, 0.69f, TEXT, shorten(track->title, 52));
                drawText(font, 570, y + 17, 0.52f, MUTED, shorten(track->artist, 34));
            }
        }
        if (!searchQuery.empty() && songs.empty()) drawText(font, 30, 222, 0.64f, MUTED, "No se encontraron coincidencias.");
    } else if (tab == LibraryTab::Playlists) {
        const int playlistCount = static_cast<int>(playlists.playlists().size());
        const int total = playlistCount + 1;
        const int end = std::min(scroll + VISIBLE_ROWS, total);
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            if (i == playlistCount) {
                drawText(font, 38, y + 17, 0.71f, ACCENT, "+ Crear nueva playlist");
            } else {
                const Playlist& playlist = playlists.playlists()[i];
                drawText(font, 38, y + 17, 0.71f, TEXT, shorten(playlist.name, 58));
                drawText(font, 790, y + 17, 0.54f, MUTED,
                         std::to_string(playlist.tracks.size()) + (playlist.tracks.size() == 1 ? " cancion" : " canciones"));
            }
        }
    } else if (tab == LibraryTab::Categories) {
        const int categoryCount = static_cast<int>(smart.categories().size());
        const int total = categoryCount + 1;
        const int end = std::min(scroll + VISIBLE_ROWS, total);
        for (int i = scroll; i < end; ++i) {
            const int y = listTop + (i - scroll) * ROW_H;
            if (i == selected) {
                vita2d_draw_rectangle(20, y - 2, 920, ROW_H - 2, ACCENT_SOFT);
                vita2d_draw_rectangle(20, y - 2, 5, ROW_H - 2, ACCENT);
            }
            if (i == categoryCount) {
                drawText(font, 38, y + 17, 0.71f, ACCENT, "+ Crear nueva categoria");
            } else {
                const SmartCategory& category = smart.categories()[i];
                drawText(font, 38, y + 17, 0.71f, TEXT, shorten(category.name, 58));
                drawText(font, 790, y + 17, 0.54f, MUTED,
                         std::to_string(category.tracks.size()) + (category.tracks.size() == 1 ? " cancion" : " canciones"));
            }
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
            if (tab == LibraryTab::Folders) drawText(font, 470, y + 17, 0.51f, MUTED, shorten(groups[i].key, 47));
            else drawText(font, 790, y + 17, 0.54f, MUTED,
                          std::to_string(groups[i].count) + (groups[i].count == 1 ? " cancion" : " canciones"));
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

void drawPlaylistTracks(vita2d_pgf* font, const std::string& playlistName,
                        const std::vector<const TrackMetadata*>& songs, int selected, int scroll,
                        const std::string& selectedSong, const TrackMetadata& metadata,
                        const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, shorten(playlistName, 42),
               "X reproducir  |  △ anadir canciones  |  □ quitar  |  SELECT ahora suena");

    const int listTop = 92;
    const int visible = 10;
    const int end = std::min(scroll + visible, static_cast<int>(songs.size()));
    if (songs.empty()) {
        drawText(font, 30, 145, 0.72f, MUTED, "Esta playlist esta vacia. Pulsa △ para anadir canciones.");
    }
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

void drawPlaylistAddSongs(vita2d_pgf* font, const LibraryManager& library, const PlaylistManager& playlists,
                          const std::string& playlistName, int selected, int scroll,
                          const std::string& selectedSong, const TrackMetadata& metadata,
                          const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Anadir canciones", shorten(playlistName, 44) + "  |  X anadir  |  O volver");

    const auto songs = sortedSongs(library);
    const Playlist* playlist = playlists.find(playlistName);
    const int listTop = 92;
    const int visible = 10;
    const int end = std::min(scroll + visible, static_cast<int>(songs.size()));
    if (songs.empty()) drawText(font, 30, 145, 0.72f, MUTED, "La biblioteca esta vacia.");

    for (int i = scroll; i < end; ++i) {
        const int y = listTop + (i - scroll) * 36;
        if (i == selected) {
            vita2d_draw_rectangle(20, y - 2, 920, 34, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 34, ACCENT);
        }
        const bool alreadyAdded = playlist &&
            std::find(playlist->tracks.begin(), playlist->tracks.end(), songs[i]->path) != playlist->tracks.end();
        drawText(font, 38, y + 18, 0.60f, alreadyAdded ? ACCENT : MUTED, alreadyAdded ? "[OK]" : "[ +]");
        drawText(font, 96, y + 18, 0.70f, TEXT, shorten(songs[i]->title, 49));
        drawText(font, 615, y + 18, 0.52f, MUTED, shorten(songs[i]->artist, 25));
    }

    drawMiniPlayer(font, selectedSong, metadata, cover, player);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}


void drawCategoryTracks(vita2d_pgf* font, const std::string& categoryName,
                        const std::vector<const TrackMetadata*>& songs, int selected, int scroll,
                        const std::string& selectedSong, const TrackMetadata& metadata,
                        const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, shorten(categoryName, 42),
               "X reproducir  |  △ anadir canciones  |  □ quitar  |  SELECT ahora suena");

    const int listTop = 92;
    const int visible = 10;
    const int end = std::min(scroll + visible, static_cast<int>(songs.size()));
    if (songs.empty()) drawText(font, 30, 145, 0.72f, MUTED, "Esta categoria esta vacia. Pulsa △ para anadir canciones.");
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

void drawCategoryAddSongs(vita2d_pgf* font, const LibraryManager& library,
                          const SmartLibraryManager& smart, const std::string& categoryName,
                          int selected, int scroll, const std::string& selectedSong,
                          const TrackMetadata& metadata, const CoverArt& cover, const AudioPlayer& player) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Anadir a categoria", shorten(categoryName, 44) + "  |  X anadir  |  O volver");

    const auto songs = sortedSongs(library);
    const SmartCategory* category = smart.findCategory(categoryName);
    const int listTop = 92;
    const int visible = 10;
    const int end = std::min(scroll + visible, static_cast<int>(songs.size()));
    for (int i = scroll; i < end; ++i) {
        const int y = listTop + (i - scroll) * 36;
        if (i == selected) {
            vita2d_draw_rectangle(20, y - 2, 920, 34, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 34, ACCENT);
        }
        const bool alreadyAdded = category &&
            std::find(category->tracks.begin(), category->tracks.end(), songs[i]->path) != category->tracks.end();
        drawText(font, 38, y + 18, 0.60f, alreadyAdded ? ACCENT : MUTED, alreadyAdded ? "[OK]" : "[ +]");
        drawText(font, 96, y + 18, 0.70f, TEXT, shorten(songs[i]->title, 49));
        drawText(font, 615, y + 18, 0.52f, MUTED, shorten(songs[i]->artist, 25));
    }
    drawMiniPlayer(font, selectedSong, metadata, cover, player);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

const char* visualizerName(int mode) {
    switch (mode) {
        case 1: return "Espectro";
        case 2: return "Onda";
        case 3: return "Circular";
        case 4: return "Caratula + espectro";
        case 0:
        default: return "Caratula";
    }
}

void drawNowPlayingCommon(vita2d_pgf* font, const AudioPlayer& player, int visualizerMode,
                          bool shuffleEnabled, int repeatMode) {
    drawProgressBar(player, 46, 476, 868, 7);
    drawText(font, 46, 505, 0.61f, MUTED, formatTime(player.positionMs()));
    drawText(font, 871, 505, 0.61f, MUTED, formatTime(player.durationMs()));

    const std::string state = player.isPaused() ? "PAUSADO" :
        (player.isPlaying() ? "REPRODUCIENDO" : player.stateLabel());
    drawText(font, 46, 464, 0.54f, ACCENT, state);
    std::string modes = std::string("Vista: ") + visualizerName(visualizerMode) +
        "  |  Aleatorio: " + (shuffleEnabled ? "ON" : "OFF") +
        "  |  Repetir: " + repeatModeName(repeatMode);
    drawText(font, 540, 464, 0.49f, MUTED, shorten(modes, 60));

    drawText(font, 56, 531, 0.50f, MUTED,
             "↑/↓ visualizador   X pausa   □ aleatorio   START repetir   △ opciones");
}

void drawSpectrumBars(const AudioVisualizer& visualizer, float x, float y, float w, float h,
                      int bars = AudioVisualizer::kBandCount) {
    const auto& spectrum = visualizer.bands();
    if (bars < 1) return;
    const float gap = 6.0f;
    const float barW = (w - gap * (bars - 1)) / bars;
    const float baseline = y + h;

    vita2d_draw_line(x, baseline, x + w, baseline, rgba(67, 69, 80));
    for (int i = 0; i < bars; ++i) {
        const int source = (i * AudioVisualizer::kBandCount) / bars;
        const float value = std::max(0.012f, spectrum[source]);
        const float barH = std::max(3.0f, value * h);
        const float bx = x + i * (barW + gap);
        vita2d_draw_rectangle(bx, baseline - barH, barW, barH, ACCENT);
        vita2d_draw_rectangle(bx, baseline - barH, barW, 2.0f, TEXT);
    }
}

void drawWaveform(const AudioVisualizer& visualizer, float x, float y, float w, float h) {
    const auto& wave = visualizer.waveform();
    const float center = y + h * 0.5f;
    const float amplitude = h * 0.43f;
    vita2d_draw_line(x, center, x + w, center, rgba(67, 69, 80));

    float previousX = x;
    float previousY = center;
    constexpr int stride = 2;
    for (int i = 0; i < AudioVisualizer::kSampleCount; i += stride) {
        const float px = x + (static_cast<float>(i) / (AudioVisualizer::kSampleCount - 1)) * w;
        const float py = center - wave[i] * amplitude;
        if (i > 0) vita2d_draw_line(previousX, previousY, px, py, ACCENT);
        previousX = px;
        previousY = py;
    }
}

void drawCircularVisualizer(const AudioVisualizer& visualizer, float cx, float cy,
                            float innerRadius, float maxExtension) {
    const auto& spectrum = visualizer.bands();
    constexpr int rays = 64;
    constexpr float pi = 3.14159265358979323846f;

    vita2d_draw_fill_circle(cx, cy, innerRadius - 8.0f, PANEL_2);
    for (int i = 0; i < rays; ++i) {
        const float angle = -pi * 0.5f + (2.0f * pi * i) / rays;
        const float value = spectrum[i % AudioVisualizer::kBandCount];
        const float outer = innerRadius + 8.0f + value * maxExtension;
        const float x0 = cx + std::cos(angle) * innerRadius;
        const float y0 = cy + std::sin(angle) * innerRadius;
        const float x1 = cx + std::cos(angle) * outer;
        const float y1 = cy + std::sin(angle) * outer;
        vita2d_draw_line(x0, y0, x1, y1, ACCENT);
    }
}

void drawNowPlaying(vita2d_pgf* font, const TrackMetadata& metadata,
                    const CoverArt& cover, const AudioPlayer& player,
                    const AudioVisualizer& visualizer, int visualizerMode,
                    bool shuffleEnabled, int repeatMode) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Ahora suena", "↑/↓: cambiar visualizador  |  O: volver  |  △: opciones");

    if (visualizerMode == 0) {
        constexpr float coverX = 46.0f;
        constexpr float coverY = 104.0f;
        constexpr float coverSize = 316.0f;
        vita2d_draw_rectangle(coverX - 4, coverY - 4, coverSize + 8, coverSize + 8, PANEL_2);
        if (cover.hasTexture()) drawTextureSquareCrop(cover.texture(), coverX, coverY, coverSize, coverSize);
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

        if (!cover.sourceLabel().empty())
            drawText(font, 46, 443, 0.54f, MUTED, "Caratula: " + shorten(cover.sourceLabel(), 38));
    } else if (visualizerMode == 1) {
        vita2d_draw_rectangle(46, 106, 868, 302, PANEL_2);
        drawSpectrumBars(visualizer, 70, 134, 820, 238);
        drawText(font, 70, 414, 0.92f, TEXT, shorten(metadata.title, 53));
        drawText(font, 70, 439, 0.61f, ACCENT, shorten(metadata.artist, 58));
    } else if (visualizerMode == 2) {
        vita2d_draw_rectangle(46, 106, 868, 302, PANEL_2);
        drawWaveform(visualizer, 70, 132, 820, 238);
        drawText(font, 70, 414, 0.92f, TEXT, shorten(metadata.title, 53));
        drawText(font, 70, 439, 0.61f, ACCENT, shorten(metadata.artist, 58));
    } else if (visualizerMode == 3) {
        drawCircularVisualizer(visualizer, 480.0f, 262.0f, 118.0f, 82.0f);
        constexpr float innerCover = 176.0f;
        const float cx = 480.0f - innerCover * 0.5f;
        const float cy = 262.0f - innerCover * 0.5f;
        if (cover.hasTexture()) drawTextureSquareCrop(cover.texture(), cx, cy, innerCover, innerCover);
        else drawCoverPlaceholder(font, cx, cy, innerCover, innerCover);
        drawText(font, 250, 411, 0.90f, TEXT, shorten(metadata.title, 46));
        drawText(font, 340, 437, 0.60f, ACCENT, shorten(metadata.artist, 38));
    } else {
        constexpr float coverX = 64.0f;
        constexpr float coverY = 124.0f;
        constexpr float coverSize = 248.0f;
        vita2d_draw_rectangle(coverX - 4, coverY - 4, coverSize + 8, coverSize + 8, PANEL_2);
        if (cover.hasTexture()) drawTextureSquareCrop(cover.texture(), coverX, coverY, coverSize, coverSize);
        else drawCoverPlaceholder(font, coverX, coverY, coverSize, coverSize);

        drawSpectrumBars(visualizer, 355, 146, 550, 200, 24);
        drawText(font, 355, 378, 0.91f, TEXT, shorten(metadata.title, 35));
        drawText(font, 355, 407, 0.65f, ACCENT, shorten(metadata.artist, 43));
        drawText(font, 64, 407, 0.58f, MUTED, shorten(metadata.album, 31));
    }

    drawNowPlayingCommon(font, player, visualizerMode, shuffleEnabled, repeatMode);
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

void drawConfirmDelete(vita2d_pgf* font, const std::string& itemName,
                       const std::string& detail, bool yesSelected) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Confirmar eliminacion", "Esta accion requiere confirmacion");

    const int boxX = 125;
    const int boxY = 145;
    const int boxW = 710;
    const int boxH = 245;
    vita2d_draw_rectangle(boxX, boxY, boxW, boxH, PANEL);
    vita2d_draw_rectangle(boxX, boxY, boxW, 5, ACCENT);

    drawText(font, 165, 205, 0.82f, TEXT, "¿Seguro que quieres eliminar?");
    drawText(font, 165, 244, 0.72f, ACCENT, shorten(itemName, 63));
    drawText(font, 165, 285, 0.58f, MUTED, detail);

    const int noX = 215;
    const int yesX = 525;
    const int buttonY = 326;
    const int buttonW = 220;
    const int buttonH = 46;

    vita2d_draw_rectangle(noX, buttonY, buttonW, buttonH, yesSelected ? PANEL_2 : ACCENT_SOFT);
    vita2d_draw_rectangle(yesX, buttonY, buttonW, buttonH, yesSelected ? ACCENT_SOFT : PANEL_2);
    if (!yesSelected) vita2d_draw_rectangle(noX, buttonY, 5, buttonH, ACCENT);
    if (yesSelected) vita2d_draw_rectangle(yesX, buttonY, 5, buttonH, ACCENT);

    drawText(font, noX + 77, buttonY + 31, 0.72f, TEXT, "No");
    drawText(font, yesX + 52, buttonY + 31, 0.72f, TEXT, "Si, eliminar");
    drawText(font, 254, 430, 0.58f, MUTED, "Izquierda/Derecha: elegir   X: confirmar   O: cancelar");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawQueue(vita2d_pgf* font, const LibraryManager& library,
               const std::vector<std::string>& queue, int currentIndex,
               int selected, int scroll) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Cola de reproduccion", "X reproducir  |  □ eliminar  |  L/R mover  |  O volver");
    const int visible = 11;
    const int end = std::min(scroll + visible, static_cast<int>(queue.size()));
    if (queue.empty()) drawText(font, 32, 132, 0.72f, MUTED, "La cola esta vacia.");
    for (int i = scroll; i < end; ++i) {
        const int y = 92 + (i - scroll) * 36;
        if (i == selected) {
            vita2d_draw_rectangle(20, y - 2, 920, 34, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 34, ACCENT);
        }
        const TrackMetadata* track = library.findTrack(queue[i]);
        const std::string title = track ? track->title : filenameFromPath(queue[i]);
        const std::string artist = track ? track->artist : "";
        drawText(font, 38, y + 18, 0.69f, i == currentIndex ? ACCENT : TEXT,
                 std::string(i == currentIndex ? "> " : "  ") + shorten(title, 55));
        drawText(font, 620, y + 18, 0.52f, MUTED, shorten(artist, 28));
    }
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawAppearance(vita2d_pgf* font, int hue, int savedHue,
                    int scalePercent, int savedScalePercent, int selectedField) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Apariencia", "Color y escala general de la interfaz");

    const int barX = 70;
    const int barW = 820;
    const int hueY = 190;
    const int barH = 38;
    constexpr int segments = 180;

    if (selectedField == 0) {
        vita2d_draw_rectangle(52, 122, 856, 122, ACCENT_SOFT);
        vita2d_draw_rectangle(52, 122, 5, 122, ACCENT);
    }
    drawText(font, 70, 151, 0.68f, TEXT, "Color HUE");
    drawText(font, 640, 151, 0.50f, MUTED, "←/→ 1°   L/R 10°");
    for (int i = 0; i < segments; ++i) {
        const int segmentHue = (i * 360) / segments;
        const RgbColor c = hueToRgb(segmentHue);
        const float x0 = barX + (static_cast<float>(i) * barW / segments);
        const float x1 = barX + (static_cast<float>(i + 1) * barW / segments);
        vita2d_draw_rectangle(x0, hueY, x1 - x0 + 1.0f, barH, rgba(c.r, c.g, c.b));
    }
    const float markerX = barX + (static_cast<float>(normalizeHue(hue)) / 359.0f) * barW;
    vita2d_draw_rectangle(markerX - 3.0f, hueY - 6.0f, 6.0f, barH + 12.0f, TEXT);

    const RgbColor selected = hueToRgb(hue);
    char colorLabel[96];
    std::snprintf(colorLabel, sizeof(colorLabel), "HUE %03d°   RGB %u,%u,%u   #%02X%02X%02X",
                  normalizeHue(hue), selected.r, selected.g, selected.b,
                  selected.r, selected.g, selected.b);
    drawText(font, 70, 242, 0.57f, ACCENT, colorLabel);

    if (selectedField == 1) {
        vita2d_draw_rectangle(52, 270, 856, 152, ACCENT_SOFT);
        vita2d_draw_rectangle(52, 270, 5, 152, ACCENT);
    }
    drawText(font, 70, 302, 0.68f, TEXT, "Escala de interfaz");
    drawText(font, 640, 302, 0.50f, MUTED, "←/→ 5%   L/R 10%");

    vita2d_draw_rectangle(70, 327, 820, 18, PANEL_2);
    const float scaleProgress = static_cast<float>(scalePercent - 80) / 40.0f;
    vita2d_draw_rectangle(70, 327, 820.0f * std::max(0.0f, std::min(1.0f, scaleProgress)), 18, ACCENT);
    const float scaleMarker = 70.0f + 820.0f * scaleProgress;
    vita2d_draw_rectangle(scaleMarker - 3.0f, 320, 6, 32, TEXT);

    drawText(font, 70, 382, 0.62f, MUTED, "80%");
    drawText(font, 449, 382, 0.62f, MUTED, "100%");
    drawText(font, 838, 382, 0.62f, MUTED, "120%");
    drawText(font, 70, 414, 0.72f, TEXT, "Vista previa: PengPlayer • Musica • 123");
    drawText(font, 700, 414, 0.70f, ACCENT, std::to_string(scalePercent) + "%");

    const bool dirty = normalizeHue(hue) != normalizeHue(savedHue) || scalePercent != savedScalePercent;
    drawText(font, 28, 518, 0.55f, dirty ? MUTED : ACCENT,
             dirty ? "↑/↓ elegir ajuste  |  X guardar  |  O cancelar   •   Sin guardar"
                   : "↑/↓ elegir ajuste  |  X guardar  |  O volver   •   Guardado");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void drawImagePicker(vita2d_pgf* font, const ImageBrowser& browser) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(font, "Elegir caratula", "X: abrir/elegir imagen  |  O: atras");
    vita2d_draw_rectangle(20, 82, 920, 28, PANEL_2);
    drawText(font, 32, 103, 0.62f, MUTED, shorten(browser.currentPath(), 92));

    const auto& entries = browser.entries();
    const int start = browser.scrollOffset();
    const int end = std::min(start + 11, static_cast<int>(entries.size()));
    if (!browser.lastError().empty()) drawText(font, 32, 150, 0.68f, ERROR_COLOR, browser.lastError());
    if (entries.empty() && browser.lastError().empty()) {
        drawText(font, 32, 150, 0.68f, MUTED, "No hay imagenes JPG o PNG en esta carpeta.");
    }
    for (int i = start; i < end; ++i) {
        const int y = 126 + (i - start) * 31;
        if (i == browser.selectedIndex()) {
            vita2d_draw_rectangle(20, y - 2, 920, 29, ACCENT_SOFT);
            vita2d_draw_rectangle(20, y - 2, 5, 29, ACCENT);
        }
        drawText(font, 38, y + 19, 0.67f, entries[i].isDirectory ? ACCENT : MUTED,
                 entries[i].isDirectory ? "[DIR]" : "[IMG]");
        drawText(font, 126, y + 19, 0.70f, TEXT, shorten(entries[i].name, 70));
    }
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
    TextInput::initialize();

    LibraryManager library;
    library.initialize();

    PreferencesManager preferences;
    preferences.initialize();
    applyAccentHue(preferences.accentHue());

    PlaylistManager playlists;
    playlists.initialize();

    SmartLibraryManager smart;
    smart.initialize();

    UI_SCALE = preferences.uiScalePercent() / 100.0f;

    AudioPlayer player;
    TrackMetadata metadata;
    CoverArt cover;
    AudioVisualizer visualizer;
    int visualizerMode = preferences.visualizerMode();
    bool shuffleEnabled = preferences.shuffleEnabled();
    int repeatMode = preferences.repeatMode();
    std::string selectedSong;

    Screen screen = Screen::Library;
    Screen returnScreen = Screen::Library;
    Screen settingsReturnScreen = Screen::Library;
    Screen playlistReturnScreen = Screen::Playlists;
    Screen confirmReturnScreen = Screen::Library;
    ConfirmAction confirmAction = ConfirmAction::None;
    std::string confirmItemName;
    int confirmIndex = -1;
    bool confirmYesSelected = false;
    LibraryTab tab = LibraryTab::Songs;
    LibraryTab groupTab = LibraryTab::Artists;
    std::string groupKey;
    std::string groupLabel;

    int selected = 0;
    int scroll = 0;
    int groupSelected = 0;
    int groupScroll = 0;
    int settingsSelected = 0;
    int appearanceHue = preferences.accentHue();
    int appearanceScale = preferences.uiScalePercent();
    int appearanceField = 0;
    int songOptionsSelected = 0;
    std::string coverTargetSong;
    int rootsSelected = 0;
    int mountsSelected = 0;
    int coverMountsSelected = 0;
    int queueSelected = 0;
    int queueScroll = 0;
    int playlistsSelected = 0;
    int playlistTracksSelected = 0;
    int playlistTracksScroll = 0;
    int playlistAddSelected = 0;
    int playlistAddScroll = 0;
    int addPlaylistSelected = 0;
    std::string activePlaylist;
    int categoryTracksSelected = 0;
    int categoryTracksScroll = 0;
    int categoryAddSelected = 0;
    int categoryAddScroll = 0;
    int addCategorySelected = 0;
    std::string activeCategory;
    std::string searchQuery;

    std::unique_ptr<MusicBrowser> folderPicker;
    std::unique_ptr<ImageBrowser> coverPicker;
    std::vector<std::string> queueOriginal;
    std::vector<std::string> playbackQueue;
    int playbackIndex = -1;
    unsigned int shuffleState = 0xC0FFEEu;

    auto startTrack = [&](const std::string& path) {
        if (!player.playFile(path)) return;
        selectedSong = path;
        const TrackMetadata* cached = library.findTrack(path);
        metadata = cached ? *cached : MetadataReader::read(path);
        cover.loadForTrack(path, preferences.customCoverFor(path));
        smart.markPlayed(path);
    };

    auto rebuildQueue = [&](const std::string& currentPath) {
        playbackQueue = queueOriginal;
        if (shuffleEnabled && playbackQueue.size() > 1) {
            auto it = std::find(playbackQueue.begin(), playbackQueue.end(), currentPath);
            if (it != playbackQueue.end()) playbackQueue.erase(it);
            for (int i = static_cast<int>(playbackQueue.size()) - 1; i > 0; --i) {
                shuffleState = shuffleState * 1664525u + 1013904223u;
                const int j = static_cast<int>(shuffleState % static_cast<unsigned int>(i + 1));
                std::swap(playbackQueue[i], playbackQueue[j]);
            }
            if (!currentPath.empty()) playbackQueue.insert(playbackQueue.begin(), currentPath);
        }
        playbackIndex = -1;
        if (!currentPath.empty()) {
            auto it = std::find(playbackQueue.begin(), playbackQueue.end(), currentPath);
            if (it != playbackQueue.end()) playbackIndex = static_cast<int>(it - playbackQueue.begin());
        }
    };

    auto setQueue = [&](const std::vector<const TrackMetadata*>& tracks, int index) {
        queueOriginal.clear();
        for (const auto* track : tracks) queueOriginal.push_back(track->path);
        std::string initial;
        if (index >= 0 && index < static_cast<int>(queueOriginal.size())) initial = queueOriginal[index];
        rebuildQueue(initial);
        if (playbackIndex >= 0) startTrack(playbackQueue[playbackIndex]);
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

    auto toggleShuffle = [&]() {
        const std::string current = selectedSong;
        shuffleEnabled = !shuffleEnabled;
        preferences.setShuffleEnabled(shuffleEnabled);
        rebuildQueue(current);
    };

    auto cycleRepeat = [&]() {
        repeatMode = (repeatMode + 1) % 3;
        preferences.setRepeatMode(repeatMode);
    };

    auto queueMove = [&](int from, int to) {
        if (from < 0 || to < 0 || from >= static_cast<int>(playbackQueue.size()) ||
            to >= static_cast<int>(playbackQueue.size()) || from == to) return;
        std::swap(playbackQueue[from], playbackQueue[to]);
        if (playbackIndex == from) playbackIndex = to;
        else if (playbackIndex == to) playbackIndex = from;
        queueSelected = to;
    };

    auto saveCurrentSession = [&]() {
        if (selectedSong.empty()) return;
        SessionState state;
        state.valid = true;
        state.currentPath = selectedSong;
        state.positionMs = player.positionMs();
        state.currentIndex = playbackIndex;
        state.paused = player.isPaused();
        state.queue = playbackQueue;
        if (state.queue.empty()) {
            state.queue.push_back(selectedSong);
            state.currentIndex = 0;
        }
        smart.saveSession(state);
    };

    const SessionState restoredSession = smart.loadSession();
    if (restoredSession.valid) {
        playbackQueue = restoredSession.queue;
        if (playbackQueue.empty()) playbackQueue.push_back(restoredSession.currentPath);
        queueOriginal = playbackQueue;
        playbackIndex = restoredSession.currentIndex;
        if (playbackIndex < 0 || playbackIndex >= static_cast<int>(playbackQueue.size())) {
            auto it = std::find(playbackQueue.begin(), playbackQueue.end(), restoredSession.currentPath);
            playbackIndex = it == playbackQueue.end() ? 0 : static_cast<int>(it - playbackQueue.begin());
        }
        selectedSong = restoredSession.currentPath;
        const TrackMetadata* cached = library.findTrack(selectedSong);
        metadata = cached ? *cached : MetadataReader::read(selectedSong);
        cover.loadForTrack(selectedSong, preferences.customCoverFor(selectedSong));
        // Restauramos en pausa para que abrir PengPlayer nunca empiece a sonar por sorpresa.
        player.playFile(selectedSong, restoredSession.positionMs, true);
    }

    int sessionFrameCounter = 0;
    SceCtrlData pad{};
    unsigned int previousButtons = 0;
    bool running = true;

    while (running) {
        library.updateScan(1);

        sceCtrlPeekBufferPositive(0, &pad, 1);
        const unsigned int pressed = pad.buttons & ~previousButtons;
        previousButtons = pad.buttons;

        if (screen == Screen::Library) {
            int count = 0;
            if (tab == LibraryTab::Songs) count = static_cast<int>(sortedSongs(library).size());
            else if (tab == LibraryTab::Playlists) count = static_cast<int>(playlists.playlists().size()) + 1;
            else if (tab == LibraryTab::Categories) count = static_cast<int>(smart.categories().size()) + 1;
            else if (tab == LibraryTab::Favorites || tab == LibraryTab::Recent || tab == LibraryTab::MostPlayed)
                count = static_cast<int>(tracksForSmartTab(library, smart, tab, searchQuery).size());
            else if (tab == LibraryTab::Search) count = static_cast<int>(searchTracks(library, searchQuery).size()) + 1;
            else count = static_cast<int>(buildGroups(library, tab).size());

            clampList(count, VISIBLE_ROWS, selected, scroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, count, VISIBLE_ROWS, selected, scroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, count, VISIBLE_ROWS, selected, scroll);

            if (pressed & SCE_CTRL_TRIANGLE) {
                tab = static_cast<LibraryTab>((static_cast<int>(tab) + 1) % kLibraryTabCount);
                selected = 0;
                scroll = 0;
            }

            if (pressed & SCE_CTRL_CROSS) {
                if (tab == LibraryTab::Songs) {
                    const auto songs = sortedSongs(library);
                    if (selected >= 0 && selected < static_cast<int>(songs.size())) setQueue(songs, selected);
                } else if (tab == LibraryTab::Favorites || tab == LibraryTab::Recent || tab == LibraryTab::MostPlayed) {
                    const auto songs = tracksForSmartTab(library, smart, tab, searchQuery);
                    if (selected >= 0 && selected < static_cast<int>(songs.size())) setQueue(songs, selected);
                } else if (tab == LibraryTab::Search) {
                    const auto songs = searchTracks(library, searchQuery);
                    if (selected == 0) {
                        const std::string query = TextInput::prompt("Buscar en la biblioteca", searchQuery, 64);
                        if (!query.empty()) searchQuery = query;
                        selected = 0;
                        scroll = 0;
                    } else if (selected - 1 < static_cast<int>(songs.size())) {
                        setQueue(songs, selected - 1);
                    }
                } else if (tab == LibraryTab::Playlists) {
                    const int playlistCount = static_cast<int>(playlists.playlists().size());
                    if (selected == playlistCount) {
                        const std::string requestedName = TextInput::prompt("Nombre de la playlist", "", 48);
                        if (!requestedName.empty()) {
                            activePlaylist = playlists.createPlaylist(requestedName);
                            selected = std::max(0, static_cast<int>(playlists.playlists().size()) - 1);
                            scroll = std::max(0, selected - VISIBLE_ROWS + 1);
                        }
                    } else if (selected >= 0 && selected < playlistCount) {
                        activePlaylist = playlists.playlists()[selected].name;
                        playlistTracksSelected = 0;
                        playlistTracksScroll = 0;
                        playlistReturnScreen = Screen::Library;
                        screen = Screen::PlaylistTracks;
                    }
                } else if (tab == LibraryTab::Categories) {
                    const int categoryCount = static_cast<int>(smart.categories().size());
                    if (selected == categoryCount) {
                        const std::string requestedName = TextInput::prompt("Nombre de la categoria", "", 48);
                        if (!requestedName.empty()) {
                            activeCategory = smart.createCategory(requestedName);
                            selected = std::max(0, static_cast<int>(smart.categories().size()) - 1);
                            scroll = std::max(0, selected - VISIBLE_ROWS + 1);
                        }
                    } else if (selected >= 0 && selected < categoryCount) {
                        activeCategory = smart.categories()[selected].name;
                        categoryTracksSelected = 0;
                        categoryTracksScroll = 0;
                        screen = Screen::CategoryTracks;
                    }
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

            if ((pressed & SCE_CTRL_SQUARE) && tab == LibraryTab::Playlists &&
                selected >= 0 && selected < static_cast<int>(playlists.playlists().size())) {
                confirmReturnScreen = Screen::Library;
                confirmAction = ConfirmAction::DeletePlaylist;
                confirmItemName = playlists.playlists()[selected].name;
                confirmIndex = selected;
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            } else if ((pressed & SCE_CTRL_SQUARE) && tab == LibraryTab::Categories &&
                       selected >= 0 && selected < static_cast<int>(smart.categories().size())) {
                confirmReturnScreen = Screen::Library;
                confirmAction = ConfirmAction::DeleteCategory;
                confirmItemName = smart.categories()[selected].name;
                confirmIndex = selected;
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            } else if ((pressed & SCE_CTRL_SQUARE) && tab == LibraryTab::Search) {
                const std::string query = TextInput::prompt("Buscar en la biblioteca", searchQuery, 64);
                if (!query.empty()) searchQuery = query;
                selected = 0;
                scroll = 0;
            }

            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::Library;
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
            if (pressed & SCE_CTRL_SQUARE) toggleShuffle();
            if (pressed & SCE_CTRL_START) cycleRepeat();
            if (pressed & SCE_CTRL_UP) {
                visualizerMode = (visualizerMode + 4) % 5;
                preferences.setVisualizerMode(visualizerMode);
            }
            if (pressed & SCE_CTRL_DOWN) {
                visualizerMode = (visualizerMode + 1) % 5;
                preferences.setVisualizerMode(visualizerMode);
            }
            if ((pressed & SCE_CTRL_TRIANGLE) && !selectedSong.empty()) {
                coverTargetSong = selectedSong;
                songOptionsSelected = 0;
                screen = Screen::SongOptions;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = returnScreen;
        } else if (screen == Screen::Settings) {
            const int count = 5;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, settingsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, settingsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                if (settingsSelected == 0) {
                    library.startScan();
                    screen = settingsReturnScreen;
                } else if (settingsSelected == 1) {
                    rootsSelected = 0;
                    screen = Screen::Roots;
                } else if (settingsSelected == 2) {
                    appearanceHue = preferences.accentHue();
                    appearanceScale = preferences.uiScalePercent();
                    appearanceField = 0;
                    applyAccentHue(appearanceHue);
                    UI_SCALE = appearanceScale / 100.0f;
                    screen = Screen::Appearance;
                } else if (settingsSelected == 3) {
                    playlistsSelected = 0;
                    playlistReturnScreen = Screen::Playlists;
                    screen = Screen::Playlists;
                } else {
                    running = false;
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = settingsReturnScreen;
        } else if (screen == Screen::Appearance) {
            if (pressed & SCE_CTRL_UP) appearanceField = 0;
            if (pressed & SCE_CTRL_DOWN) appearanceField = 1;
            if (appearanceField == 0) {
                bool changed = false;
                if (pressed & SCE_CTRL_LEFT) { appearanceHue = normalizeHue(appearanceHue - 1); changed = true; }
                if (pressed & SCE_CTRL_RIGHT) { appearanceHue = normalizeHue(appearanceHue + 1); changed = true; }
                if (pressed & SCE_CTRL_LTRIGGER) { appearanceHue = normalizeHue(appearanceHue - 10); changed = true; }
                if (pressed & SCE_CTRL_RTRIGGER) { appearanceHue = normalizeHue(appearanceHue + 10); changed = true; }
                if (changed) applyAccentHue(appearanceHue);
            } else {
                if (pressed & SCE_CTRL_LEFT) appearanceScale = std::max(80, appearanceScale - 5);
                if (pressed & SCE_CTRL_RIGHT) appearanceScale = std::min(120, appearanceScale + 5);
                if (pressed & SCE_CTRL_LTRIGGER) appearanceScale = std::max(80, appearanceScale - 10);
                if (pressed & SCE_CTRL_RTRIGGER) appearanceScale = std::min(120, appearanceScale + 10);
                UI_SCALE = appearanceScale / 100.0f;
            }
            if (pressed & SCE_CTRL_CROSS) {
                preferences.setAccentHue(appearanceHue);
                preferences.setUiScalePercent(appearanceScale);
                applyAccentHue(appearanceHue);
                UI_SCALE = appearanceScale / 100.0f;
            }
            if (pressed & SCE_CTRL_CIRCLE) {
                appearanceHue = preferences.accentHue();
                appearanceScale = preferences.uiScalePercent();
                applyAccentHue(appearanceHue);
                UI_SCALE = appearanceScale / 100.0f;
                screen = Screen::Settings;
            }
        } else if (screen == Screen::SongOptions) {
            const int count = 6;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, songOptionsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, songOptionsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                if (songOptionsSelected == 0) {
                    coverMountsSelected = 0;
                    screen = Screen::CoverMounts;
                } else if (songOptionsSelected == 1 && !coverTargetSong.empty()) {
                    preferences.removeCustomCover(coverTargetSong);
                    if (coverTargetSong == selectedSong) cover.loadForTrack(selectedSong);
                    screen = Screen::NowPlaying;
                } else if (songOptionsSelected == 2) {
                    queueSelected = playbackIndex >= 0 ? playbackIndex : 0;
                    queueScroll = 0;
                    screen = Screen::Queue;
                } else if (songOptionsSelected == 3) {
                    addPlaylistSelected = 0;
                    screen = Screen::AddToPlaylist;
                } else if (songOptionsSelected == 4 && !coverTargetSong.empty()) {
                    smart.toggleFavorite(coverTargetSong);
                } else if (songOptionsSelected == 5) {
                    addCategorySelected = 0;
                    screen = Screen::AddToCategory;
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::NowPlaying;
        } else if (screen == Screen::Queue) {
            const int count = static_cast<int>(playbackQueue.size());
            clampList(count, 11, queueSelected, queueScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, count, 11, queueSelected, queueScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, count, 11, queueSelected, queueScroll);
            if ((pressed & SCE_CTRL_CROSS) && queueSelected >= 0 && queueSelected < count) playQueueIndex(queueSelected);
            if ((pressed & SCE_CTRL_SQUARE) && queueSelected >= 0 && queueSelected < count && queueSelected != playbackIndex) {
                const std::string removed = playbackQueue[queueSelected];
                playbackQueue.erase(playbackQueue.begin() + queueSelected);
                auto oit = std::find(queueOriginal.begin(), queueOriginal.end(), removed);
                if (oit != queueOriginal.end()) queueOriginal.erase(oit);
                if (queueSelected < playbackIndex) --playbackIndex;
                if (queueSelected >= static_cast<int>(playbackQueue.size())) queueSelected = static_cast<int>(playbackQueue.size()) - 1;
                if (queueSelected < 0) queueSelected = 0;
            }
            if (pressed & SCE_CTRL_LTRIGGER) queueMove(queueSelected, queueSelected - 1);
            if (pressed & SCE_CTRL_RTRIGGER) queueMove(queueSelected, queueSelected + 1);
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::NowPlaying;
        } else if (screen == Screen::Playlists) {
            const int count = static_cast<int>(playlists.playlists().size()) + 1;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, playlistsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, playlistsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                if (playlistsSelected == static_cast<int>(playlists.playlists().size())) {
                    const std::string requestedName = TextInput::prompt("Nombre de la playlist", "", 48);
                    if (!requestedName.empty()) {
                        activePlaylist = playlists.createPlaylist(requestedName);
                        playlistsSelected = static_cast<int>(playlists.playlists().size()) - 1;
                    }
                } else if (playlistsSelected >= 0 && playlistsSelected < static_cast<int>(playlists.playlists().size())) {
                    activePlaylist = playlists.playlists()[playlistsSelected].name;
                    playlistTracksSelected = 0;
                    playlistTracksScroll = 0;
                    playlistReturnScreen = Screen::Playlists;
                    screen = Screen::PlaylistTracks;
                }
            }
            if ((pressed & SCE_CTRL_SQUARE) && playlistsSelected >= 0 && playlistsSelected < static_cast<int>(playlists.playlists().size())) {
                confirmReturnScreen = Screen::Playlists;
                confirmAction = ConfirmAction::DeletePlaylist;
                confirmItemName = playlists.playlists()[playlistsSelected].name;
                confirmIndex = playlistsSelected;
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            }
            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::Playlists;
                screen = Screen::Settings;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Settings;
        } else if (screen == Screen::PlaylistTracks) {
            const Playlist* playlist = playlists.find(activePlaylist);
            const std::vector<const TrackMetadata*> songs = playlist ? tracksFromPaths(library, playlist->tracks) : std::vector<const TrackMetadata*>();
            clampList(static_cast<int>(songs.size()), 10, playlistTracksSelected, playlistTracksScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, static_cast<int>(songs.size()), 10, playlistTracksSelected, playlistTracksScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, static_cast<int>(songs.size()), 10, playlistTracksSelected, playlistTracksScroll);
            if ((pressed & SCE_CTRL_CROSS) && playlistTracksSelected < static_cast<int>(songs.size())) setQueue(songs, playlistTracksSelected);
            if ((pressed & SCE_CTRL_SQUARE) && playlist && playlistTracksSelected < static_cast<int>(playlist->tracks.size())) {
                confirmReturnScreen = Screen::PlaylistTracks;
                confirmAction = ConfirmAction::RemovePlaylistTrack;
                confirmIndex = playlistTracksSelected;
                const auto currentSongs = tracksFromPaths(library, playlist->tracks);
                if (playlistTracksSelected >= 0 && playlistTracksSelected < static_cast<int>(currentSongs.size()))
                    confirmItemName = currentSongs[playlistTracksSelected]->title;
                else
                    confirmItemName = "Cancion seleccionada";
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            }
            if (pressed & SCE_CTRL_TRIANGLE) {
                playlistAddSelected = 0;
                playlistAddScroll = 0;
                screen = Screen::PlaylistAddSongs;
            }
            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::PlaylistTracks;
                screen = Screen::Settings;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = playlistReturnScreen;
        } else if (screen == Screen::PlaylistAddSongs) {
            const auto songs = sortedSongs(library);
            const int count = static_cast<int>(songs.size());
            clampList(count, 10, playlistAddSelected, playlistAddScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, count, 10, playlistAddSelected, playlistAddScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, count, 10, playlistAddSelected, playlistAddScroll);
            if ((pressed & SCE_CTRL_CROSS) && playlistAddSelected >= 0 && playlistAddSelected < count) {
                playlists.addTrack(activePlaylist, songs[playlistAddSelected]->path);
            }
            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::PlaylistAddSongs;
                screen = Screen::Settings;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::PlaylistTracks;
        } else if (screen == Screen::CategoryTracks) {
            const SmartCategory* category = smart.findCategory(activeCategory);
            const std::vector<const TrackMetadata*> songs = category ? tracksFromPaths(library, category->tracks) : std::vector<const TrackMetadata*>();
            clampList(static_cast<int>(songs.size()), 10, categoryTracksSelected, categoryTracksScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, static_cast<int>(songs.size()), 10, categoryTracksSelected, categoryTracksScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, static_cast<int>(songs.size()), 10, categoryTracksSelected, categoryTracksScroll);
            if ((pressed & SCE_CTRL_CROSS) && categoryTracksSelected < static_cast<int>(songs.size())) setQueue(songs, categoryTracksSelected);
            if ((pressed & SCE_CTRL_SQUARE) && category && categoryTracksSelected < static_cast<int>(category->tracks.size())) {
                confirmReturnScreen = Screen::CategoryTracks;
                confirmAction = ConfirmAction::RemoveCategoryTrack;
                confirmIndex = categoryTracksSelected;
                confirmItemName = categoryTracksSelected < static_cast<int>(songs.size()) ? songs[categoryTracksSelected]->title : "Cancion seleccionada";
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            }
            if (pressed & SCE_CTRL_TRIANGLE) {
                categoryAddSelected = 0;
                categoryAddScroll = 0;
                screen = Screen::CategoryAddSongs;
            }
            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::CategoryTracks;
                screen = Screen::Settings;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Library;
        } else if (screen == Screen::CategoryAddSongs) {
            const auto songs = sortedSongs(library);
            const int count = static_cast<int>(songs.size());
            clampList(count, 10, categoryAddSelected, categoryAddScroll);
            if (pressed & SCE_CTRL_UP) moveList(-1, count, 10, categoryAddSelected, categoryAddScroll);
            if (pressed & SCE_CTRL_DOWN) moveList(1, count, 10, categoryAddSelected, categoryAddScroll);
            if ((pressed & SCE_CTRL_CROSS) && categoryAddSelected >= 0 && categoryAddSelected < count)
                smart.addTrackToCategory(activeCategory, songs[categoryAddSelected]->path);
            if (pressed & SCE_CTRL_START) {
                settingsSelected = 0;
                settingsReturnScreen = Screen::CategoryAddSongs;
                screen = Screen::Settings;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::CategoryTracks;
        } else if (screen == Screen::AddToCategory) {
            const int count = static_cast<int>(smart.categories().size()) + 1;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, addCategorySelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, addCategorySelected);
            if (pressed & SCE_CTRL_CROSS) {
                std::string target;
                if (addCategorySelected == static_cast<int>(smart.categories().size())) {
                    const std::string requestedName = TextInput::prompt("Nombre de la categoria", "", 48);
                    if (!requestedName.empty()) target = smart.createCategory(requestedName);
                } else if (addCategorySelected >= 0 && addCategorySelected < static_cast<int>(smart.categories().size())) {
                    target = smart.categories()[addCategorySelected].name;
                }
                if (!target.empty() && !coverTargetSong.empty()) smart.addTrackToCategory(target, coverTargetSong);
                screen = Screen::NowPlaying;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::SongOptions;
        } else if (screen == Screen::AddToPlaylist) {
            const int count = static_cast<int>(playlists.playlists().size()) + 1;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, addPlaylistSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, addPlaylistSelected);
            if (pressed & SCE_CTRL_CROSS) {
                std::string target;
                if (addPlaylistSelected == static_cast<int>(playlists.playlists().size())) {
                    const std::string requestedName = TextInput::prompt("Nombre de la playlist", "", 48);
                    if (!requestedName.empty()) target = playlists.createPlaylist(requestedName);
                } else if (addPlaylistSelected >= 0 && addPlaylistSelected < static_cast<int>(playlists.playlists().size())) {
                    target = playlists.playlists()[addPlaylistSelected].name;
                }
                if (!target.empty() && !coverTargetSong.empty()) playlists.addTrack(target, coverTargetSong);
                screen = Screen::NowPlaying;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::SongOptions;
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
                confirmReturnScreen = Screen::Roots;
                confirmAction = ConfirmAction::RemoveMusicRoot;
                confirmIndex = rootsSelected;
                confirmItemName = library.roots()[rootsSelected];
                confirmYesSelected = false;
                screen = Screen::ConfirmDelete;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::Settings;
        } else if (screen == Screen::ConfirmDelete) {
            if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN))
                confirmYesSelected = !confirmYesSelected;

            if (pressed & SCE_CTRL_CIRCLE) {
                screen = confirmReturnScreen;
                confirmAction = ConfirmAction::None;
                confirmIndex = -1;
                confirmItemName.clear();
                confirmYesSelected = false;
            }

            if (pressed & SCE_CTRL_CROSS) {
                if (confirmYesSelected) {
                    if (confirmAction == ConfirmAction::DeletePlaylist) {
                        playlists.deletePlaylist(confirmItemName);
                        if (confirmReturnScreen == Screen::Library) {
                            const int newCount = static_cast<int>(playlists.playlists().size()) + 1;
                            if (selected >= newCount) selected = std::max(0, newCount - 1);
                        } else {
                            if (playlistsSelected >= static_cast<int>(playlists.playlists().size()))
                                playlistsSelected = static_cast<int>(playlists.playlists().size());
                        }
                    } else if (confirmAction == ConfirmAction::RemovePlaylistTrack) {
                        if (confirmIndex >= 0) {
                            playlists.removeTrack(activePlaylist, static_cast<std::size_t>(confirmIndex));
                            if (playlistTracksSelected > 0) --playlistTracksSelected;
                        }
                    } else if (confirmAction == ConfirmAction::RemoveMusicRoot) {
                        if (confirmIndex >= 0 && library.removeRoot(static_cast<std::size_t>(confirmIndex))) {
                            library.startScan();
                            if (rootsSelected >= static_cast<int>(library.roots().size()))
                                rootsSelected = static_cast<int>(library.roots().size()) - 1;
                            if (rootsSelected < 0) rootsSelected = 0;
                        }
                    } else if (confirmAction == ConfirmAction::DeleteCategory) {
                        smart.deleteCategory(confirmItemName);
                        const int newCount = static_cast<int>(smart.categories().size()) + 1;
                        if (selected >= newCount) selected = std::max(0, newCount - 1);
                    } else if (confirmAction == ConfirmAction::RemoveCategoryTrack) {
                        if (confirmIndex >= 0) {
                            smart.removeTrackFromCategory(activeCategory, static_cast<std::size_t>(confirmIndex));
                            if (categoryTracksSelected > 0) --categoryTracksSelected;
                        }
                    }
                }

                screen = confirmReturnScreen;
                confirmAction = ConfirmAction::None;
                confirmIndex = -1;
                confirmItemName.clear();
                confirmYesSelected = false;
            }
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
        } else if (screen == Screen::CoverMounts) {
            const int count = 3;
            if (pressed & SCE_CTRL_UP) moveSimple(-1, count, coverMountsSelected);
            if (pressed & SCE_CTRL_DOWN) moveSimple(1, count, coverMountsSelected);
            if (pressed & SCE_CTRL_CROSS) {
                static const char* mounts[] = {"ux0:/", "uma0:/", "imc0:/"};
                coverPicker.reset(new ImageBrowser(mounts[coverMountsSelected]));
                screen = Screen::CoverPicker;
            }
            if (pressed & SCE_CTRL_CIRCLE) screen = Screen::SongOptions;
        } else if (screen == Screen::CoverPicker && coverPicker) {
            if (pressed & SCE_CTRL_UP) coverPicker->moveUp();
            if (pressed & SCE_CTRL_DOWN) coverPicker->moveDown();
            if (pressed & SCE_CTRL_CROSS) {
                std::string imagePath;
                if (coverPicker->enterSelected(imagePath) && !coverTargetSong.empty()) {
                    if (preferences.setCustomCover(coverTargetSong, imagePath)) {
                        if (coverTargetSong == selectedSong) cover.loadForTrack(selectedSong, imagePath);
                        screen = Screen::NowPlaying;
                    }
                }
            }
            if (pressed & SCE_CTRL_CIRCLE) {
                if (!coverPicker->goBack()) screen = Screen::CoverMounts;
            }
        }

        // SELECT se procesa una sola vez. Antes, al usar SELECT dentro de una
        // playlist entrabamos a Ahora suena y el mismo frame lo cerraba otra vez.
        const bool canOpenNowPlaying = screen == Screen::Library ||
            screen == Screen::GroupTracks || screen == Screen::Playlists ||
            screen == Screen::PlaylistTracks || screen == Screen::PlaylistAddSongs ||
            screen == Screen::CategoryTracks || screen == Screen::CategoryAddSongs;
        if ((pressed & SCE_CTRL_SELECT) && !selectedSong.empty()) {
            if (screen == Screen::NowPlaying) {
                screen = returnScreen;
            } else if (canOpenNowPlaying) {
                returnScreen = screen;
                screen = Screen::NowPlaying;
            }
        }

        const bool playbackControls = screen == Screen::Library || screen == Screen::GroupTracks || screen == Screen::NowPlaying ||
            screen == Screen::PlaylistTracks || screen == Screen::CategoryTracks;
        if (playbackControls) {
            const bool squareReserved = screen == Screen::Library &&
                (tab == LibraryTab::Playlists || tab == LibraryTab::Categories || tab == LibraryTab::Search);
            if ((pressed & SCE_CTRL_SQUARE) && screen != Screen::NowPlaying && !squareReserved &&
                screen != Screen::PlaylistTracks && screen != Screen::CategoryTracks) player.togglePause();
            if (pressed & SCE_CTRL_LEFT) player.seekRelative(-5);
            if (pressed & SCE_CTRL_RIGHT) player.seekRelative(5);
            if (pressed & SCE_CTRL_LTRIGGER) playAdjacent(-1, true);
            if (pressed & SCE_CTRL_RTRIGGER) playAdjacent(1, true);
        }

        if (player.consumeTrackFinished()) {
            if (repeatMode == 2 && playbackIndex >= 0) playQueueIndex(playbackIndex);
            else playAdjacent(1, repeatMode == 1);
        }

        if (screen == Screen::Library) {
            drawLibrary(font, library, playlists, smart, tab, selected, scroll, searchQuery, selectedSong, metadata, cover, player);
        } else if (screen == Screen::GroupTracks) {
            const auto songs = tracksForGroup(library, groupTab, groupKey);
            drawGroupTracks(font, groupLabel, songs, groupSelected, groupScroll, selectedSong, metadata, cover, player);
        } else if (screen == Screen::NowPlaying) {
            visualizer.update(player);
            drawNowPlaying(font, metadata, cover, player, visualizer, visualizerMode, shuffleEnabled, repeatMode);
        } else if (screen == Screen::Settings) {
            std::vector<std::string> items = {"Actualizar biblioteca", "Rutas de musica", "Apariencia", "Playlists", "Salir de PengPlayer"};
            drawMenu(font, "Ajustes", "Biblioteca y personalizacion", items, settingsSelected,
                     "X: seleccionar  |  O: volver");
        } else if (screen == Screen::Appearance) {
            drawAppearance(font, appearanceHue, preferences.accentHue(),
                           appearanceScale, preferences.uiScalePercent(), appearanceField);
        } else if (screen == Screen::SongOptions) {
            std::vector<std::string> items = {"Cambiar caratula", "Restaurar caratula original",
                "Ver cola de reproduccion", "Anadir a playlist",
                smart.isFavorite(coverTargetSong) ? "Quitar de favoritos" : "Anadir a favoritos",
                "Anadir a categoria"};
            const std::string custom = preferences.customCoverFor(coverTargetSong);
            const std::string subtitle = custom.empty() ? "Organiza y personaliza esta cancion" : "Caratula personalizada: " + shorten(custom, 55);
            drawMenu(font, "Opciones de cancion", subtitle, items, songOptionsSelected,
                     "X: seleccionar  |  O: volver");
        } else if (screen == Screen::Queue) {
            drawQueue(font, library, playbackQueue, playbackIndex, queueSelected, queueScroll);
        } else if (screen == Screen::Playlists) {
            std::vector<std::string> items = playlists.names();
            items.push_back("+ Crear nueva playlist");
            drawMenu(font, "Playlists", "X abrir/crear  |  □ eliminar  |  SELECT ahora suena  |  START ajustes", items, playlistsSelected,
                     "Al crear una playlist se abre el teclado nativo para elegir su nombre");
        } else if (screen == Screen::PlaylistTracks) {
            const Playlist* playlist = playlists.find(activePlaylist);
            const std::vector<const TrackMetadata*> songs = playlist ? tracksFromPaths(library, playlist->tracks) : std::vector<const TrackMetadata*>();
            drawPlaylistTracks(font, activePlaylist, songs, playlistTracksSelected, playlistTracksScroll, selectedSong, metadata, cover, player);
        } else if (screen == Screen::PlaylistAddSongs) {
            drawPlaylistAddSongs(font, library, playlists, activePlaylist, playlistAddSelected, playlistAddScroll,
                                 selectedSong, metadata, cover, player);
        } else if (screen == Screen::AddToPlaylist) {
            std::vector<std::string> items = playlists.names();
            items.push_back("+ Crear nueva playlist");
            drawMenu(font, "Anadir a playlist", shorten(metadata.title, 58), items, addPlaylistSelected,
                     "X: anadir  |  O: volver");
        } else if (screen == Screen::CategoryTracks) {
            const SmartCategory* category = smart.findCategory(activeCategory);
            const std::vector<const TrackMetadata*> songs = category ? tracksFromPaths(library, category->tracks) : std::vector<const TrackMetadata*>();
            drawCategoryTracks(font, activeCategory, songs, categoryTracksSelected, categoryTracksScroll,
                               selectedSong, metadata, cover, player);
        } else if (screen == Screen::CategoryAddSongs) {
            drawCategoryAddSongs(font, library, smart, activeCategory, categoryAddSelected, categoryAddScroll,
                                 selectedSong, metadata, cover, player);
        } else if (screen == Screen::AddToCategory) {
            std::vector<std::string> items;
            for (const auto& category : smart.categories()) items.push_back(category.name);
            items.push_back("+ Crear nueva categoria");
            drawMenu(font, "Anadir a categoria", shorten(metadata.title, 58), items, addCategorySelected,
                     "X: anadir  |  O: volver");
        } else if (screen == Screen::ConfirmDelete) {
            std::string detail = "No se eliminara ningun archivo de musica.";
            if (confirmAction == ConfirmAction::RemovePlaylistTrack)
                detail = "Solo se quitara de esta playlist. El archivo de musica se conserva.";
            else if (confirmAction == ConfirmAction::RemoveMusicRoot)
                detail = "Solo se quitara esta ruta de PengPlayer. Tus archivos se conservan.";
            else if (confirmAction == ConfirmAction::DeletePlaylist)
                detail = "Se eliminara la playlist, pero no sus archivos de musica.";
            else if (confirmAction == ConfirmAction::DeleteCategory)
                detail = "Se eliminara la categoria, pero no sus archivos de musica.";
            else if (confirmAction == ConfirmAction::RemoveCategoryTrack)
                detail = "Solo se quitara de esta categoria. El archivo se conserva.";
            drawConfirmDelete(font, confirmItemName, detail, confirmYesSelected);
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
        } else if (screen == Screen::CoverMounts) {
            std::vector<std::string> items = {"ux0:/  Memoria principal / SD2Vita", "uma0:/  Almacenamiento secundario", "imc0:/  Memoria interna"};
            drawMenu(font, "Buscar caratula", "Selecciona donde esta guardada la imagen", items, coverMountsSelected,
                     "X: abrir  |  O: volver");
        } else if (screen == Screen::CoverPicker && coverPicker) {
            drawImagePicker(font, *coverPicker);
        }

        if (++sessionFrameCounter >= 300) {
            saveCurrentSession();
            sessionFrameCounter = 0;
        }

        sceKernelDelayThread(16000);
    }

    saveCurrentSession();
    player.stop();
    cover.clear();
    TextInput::shutdown();
    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
