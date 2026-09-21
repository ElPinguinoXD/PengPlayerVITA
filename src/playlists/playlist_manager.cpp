#include "playlist_manager.hpp"

#include <psp2/io/stat.h>

#include <algorithm>
#include <cstdio>

namespace {
const char* kDataDir = "ux0:data/PengPlayer";
const char* kPlaylistsPath = "ux0:data/PengPlayer/playlists.dat";

std::string trimLine(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}
}

void PlaylistManager::initialize() {
    ensureDataDirectory();
    load();
}

void PlaylistManager::ensureDataDirectory() {
    sceIoMkdir(kDataDir, 0777);
}

std::vector<std::string> PlaylistManager::names() const {
    std::vector<std::string> result;
    for (const auto& playlist : playlists_) result.push_back(playlist.name);
    return result;
}

const Playlist* PlaylistManager::find(const std::string& name) const {
    for (const auto& playlist : playlists_) {
        if (playlist.name == name) return &playlist;
    }
    return nullptr;
}

std::string PlaylistManager::createPlaylist(const std::string& requestedName) {
    std::string base = requestedName;

    // El formato de playlists.dat es una linea por registro, por lo que
    // normalizamos caracteres de control sin limitar UTF-8 (acentos, etc.).
    for (char& c : base) {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }

    const std::size_t first = base.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const std::size_t last = base.find_last_not_of(' ');
    base = base.substr(first, last - first + 1);
    if (base.empty()) return "";

    std::string candidate = base;
    int suffix = 2;
    while (find(candidate)) {
        candidate = base + " (" + std::to_string(suffix++) + ")";
    }

    playlists_.push_back({candidate, {}});
    save();
    return candidate;
}

bool PlaylistManager::deletePlaylist(const std::string& name) {
    auto it = std::find_if(playlists_.begin(), playlists_.end(), [&](const Playlist& p) { return p.name == name; });
    if (it == playlists_.end()) return false;
    playlists_.erase(it);
    save();
    return true;
}

bool PlaylistManager::addTrack(const std::string& playlistName, const std::string& path) {
    if (path.empty()) return false;
    for (auto& playlist : playlists_) {
        if (playlist.name != playlistName) continue;
        if (std::find(playlist.tracks.begin(), playlist.tracks.end(), path) == playlist.tracks.end())
            playlist.tracks.push_back(path);
        save();
        return true;
    }
    return false;
}

bool PlaylistManager::removeTrack(const std::string& playlistName, std::size_t index) {
    for (auto& playlist : playlists_) {
        if (playlist.name != playlistName) continue;
        if (index >= playlist.tracks.size()) return false;
        playlist.tracks.erase(playlist.tracks.begin() + static_cast<long>(index));
        save();
        return true;
    }
    return false;
}

void PlaylistManager::load() {
    playlists_.clear();
    std::FILE* file = std::fopen(kPlaylistsPath, "rb");
    if (!file) return;

    Playlist* current = nullptr;
    char line[2048];
    while (std::fgets(line, sizeof(line), file)) {
        std::string value = trimLine(line);
        if (value.size() > 2 && value[0] == 'P' && value[1] == '\t') {
            playlists_.push_back({value.substr(2), {}});
            current = &playlists_.back();
        } else if (current && value.size() > 2 && value[0] == 'T' && value[1] == '\t') {
            current->tracks.push_back(value.substr(2));
        }
    }
    std::fclose(file);
}

void PlaylistManager::save() const {
    std::FILE* file = std::fopen(kPlaylistsPath, "wb");
    if (!file) return;
    for (const auto& playlist : playlists_) {
        std::fprintf(file, "P\t%s\n", playlist.name.c_str());
        for (const auto& path : playlist.tracks) std::fprintf(file, "T\t%s\n", path.c_str());
    }
    std::fclose(file);
}
