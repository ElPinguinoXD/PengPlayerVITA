#include "library_manager.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
const char* DATA_DIR = "ux0:data/PengPlayer";
const char* ROOTS_FILE = "ux0:data/PengPlayer/roots.txt";
const char* CACHE_FILE = "ux0:data/PengPlayer/library.dat";
const char CACHE_MAGIC[8] = {'P','P','L','I','B','0','4','\0'};

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool writeU32(std::FILE* file, std::uint32_t value) {
    return std::fwrite(&value, sizeof(value), 1, file) == 1;
}

bool readU32(std::FILE* file, std::uint32_t& value) {
    return std::fread(&value, sizeof(value), 1, file) == 1;
}

bool writeI32(std::FILE* file, std::int32_t value) {
    return std::fwrite(&value, sizeof(value), 1, file) == 1;
}

bool readI32(std::FILE* file, std::int32_t& value) {
    return std::fread(&value, sizeof(value), 1, file) == 1;
}

bool writeString(std::FILE* file, const std::string& value) {
    if (value.size() > 16U * 1024U * 1024U) return false;
    if (!writeU32(file, static_cast<std::uint32_t>(value.size()))) return false;
    return value.empty() || std::fwrite(value.data(), 1, value.size(), file) == value.size();
}

bool readString(std::FILE* file, std::string& value) {
    std::uint32_t size = 0;
    if (!readU32(file, size) || size > 16U * 1024U * 1024U) return false;
    value.assign(size, '\0');
    return size == 0 || std::fread(&value[0], 1, size, file) == size;
}

bool samePath(const std::string& a, const std::string& b) {
    return lowerCopy(a) == lowerCopy(b);
}
}

LibraryManager::LibraryManager() = default;

void LibraryManager::ensureDataDirectory() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_DIR, 0777);
}

std::string LibraryManager::normalizeRoot(const std::string& path) {
    if (path.empty()) return path;
    std::string out = path;
    while (out.size() > 5 && out.back() == '/') out.pop_back();
    return out;
}

std::string LibraryManager::joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

bool LibraryManager::isAudioFile(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerCopy(name.substr(dot));
    static const char* supported[] = {
        ".mp3", ".flac", ".wav", ".ogg", ".opus", ".aiff", ".aif"
    };
    for (const char* item : supported) {
        if (ext == item) return true;
    }
    return false;
}

void LibraryManager::loadRoots() {
    roots_.clear();
    std::FILE* file = std::fopen(ROOTS_FILE, "rb");
    if (file) {
        char line[1024];
        while (std::fgets(line, sizeof(line), file)) {
            std::string value(line);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
            value = normalizeRoot(value);
            if (value.empty()) continue;

            bool duplicate = false;
            for (const auto& root : roots_) {
                if (samePath(root, value)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) roots_.push_back(value);
        }
        std::fclose(file);
    }

    if (roots_.empty()) roots_.push_back("ux0:/music");
}

void LibraryManager::saveRoots() const {
    std::FILE* file = std::fopen(ROOTS_FILE, "wb");
    if (!file) return;
    for (const auto& root : roots_) {
        std::fwrite(root.data(), 1, root.size(), file);
        std::fwrite("\n", 1, 1, file);
    }
    std::fclose(file);
}

bool LibraryManager::loadCache() {
    tracks_.clear();
    std::FILE* file = std::fopen(CACHE_FILE, "rb");
    if (!file) return false;

    char magic[8]{};
    if (std::fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
        std::memcmp(magic, CACHE_MAGIC, sizeof(magic)) != 0) {
        std::fclose(file);
        return false;
    }

    std::uint32_t count = 0;
    if (!readU32(file, count) || count > 100000U) {
        std::fclose(file);
        return false;
    }

    tracks_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TrackMetadata item;
        std::int32_t valid = 0;
        std::int32_t duration = 0;
        std::int32_t sampleRate = 0;
        std::int32_t channels = 0;
        std::int32_t bitDepth = 0;
        std::int32_t bitrate = 0;

        if (!readI32(file, valid) ||
            !readString(file, item.path) ||
            !readString(file, item.title) ||
            !readString(file, item.artist) ||
            !readString(file, item.album) ||
            !readString(file, item.genre) ||
            !readString(file, item.date) ||
            !readString(file, item.trackNumber) ||
            !readString(file, item.formatName) ||
            !readI32(file, duration) || !readI32(file, sampleRate) ||
            !readI32(file, channels) || !readI32(file, bitDepth) ||
            !readI32(file, bitrate)) {
            tracks_.clear();
            std::fclose(file);
            return false;
        }

        item.valid = valid != 0;
        item.durationMs = duration;
        item.sampleRate = sampleRate;
        item.channels = channels;
        item.bitDepth = bitDepth;
        item.bitrateKbps = bitrate;
        tracks_.push_back(item);
    }

    std::fclose(file);
    status_message_ = "Biblioteca cargada: " + std::to_string(tracks_.size()) + " canciones";
    return true;
}

void LibraryManager::saveCache() const {
    std::FILE* file = std::fopen(CACHE_FILE, "wb");
    if (!file) return;

    if (std::fwrite(CACHE_MAGIC, 1, sizeof(CACHE_MAGIC), file) != sizeof(CACHE_MAGIC) ||
        !writeU32(file, static_cast<std::uint32_t>(tracks_.size()))) {
        std::fclose(file);
        return;
    }

    for (const auto& item : tracks_) {
        if (!writeI32(file, item.valid ? 1 : 0) ||
            !writeString(file, item.path) || !writeString(file, item.title) ||
            !writeString(file, item.artist) || !writeString(file, item.album) ||
            !writeString(file, item.genre) || !writeString(file, item.date) ||
            !writeString(file, item.trackNumber) || !writeString(file, item.formatName) ||
            !writeI32(file, item.durationMs) || !writeI32(file, item.sampleRate) ||
            !writeI32(file, item.channels) || !writeI32(file, item.bitDepth) ||
            !writeI32(file, item.bitrateKbps)) {
            break;
        }
    }
    std::fclose(file);
}

void LibraryManager::initialize() {
    ensureDataDirectory();
    loadRoots();
    saveRoots();
    if (!loadCache()) startScan();
}

bool LibraryManager::addRoot(const std::string& path) {
    const std::string normalized = normalizeRoot(path);
    if (normalized.empty()) return false;
    for (const auto& root : roots_) {
        if (samePath(root, normalized)) {
            status_message_ = "Esa ruta ya esta en la biblioteca";
            return false;
        }
    }
    roots_.push_back(normalized);
    saveRoots();
    status_message_ = "Ruta anadida: " + normalized;
    return true;
}

bool LibraryManager::removeRoot(std::size_t index) {
    if (index >= roots_.size()) return false;
    if (roots_.size() <= 1) {
        status_message_ = "Debe quedar al menos una ruta de musica";
        return false;
    }
    roots_.erase(roots_.begin() + static_cast<std::ptrdiff_t>(index));
    saveRoots();
    status_message_ = "Ruta eliminada";
    return true;
}

void LibraryManager::collectAudioRecursive(const std::string& path, int depth) {
    if (depth > 32) return;

    SceUID dfd = sceIoDopen(path.c_str());
    if (dfd < 0) return;

    while (true) {
        SceIoDirent dir;
        std::memset(&dir, 0, sizeof(dir));
        const int result = sceIoDread(dfd, &dir);
        if (result <= 0) break;

        const std::string name = dir.d_name;
        if (name == "." || name == "..") continue;
        const std::string full = joinPath(path, name);

        if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
            collectAudioRecursive(full, depth + 1);
        } else if (isAudioFile(name)) {
            pending_paths_.push_back(full);
        }
    }

    sceIoDclose(dfd);
}

void LibraryManager::startScan() {
    pending_paths_.clear();
    scan_tracks_.clear();
    scan_index_ = 0;

    for (const auto& root : roots_) collectAudioRecursive(root, 0);

    std::sort(pending_paths_.begin(), pending_paths_.end());
    pending_paths_.erase(std::unique(pending_paths_.begin(), pending_paths_.end()), pending_paths_.end());

    scanning_ = true;
    status_message_ = "Escaneando biblioteca...";

    if (pending_paths_.empty()) {
        tracks_.clear();
        scanning_ = false;
        status_message_ = "No se encontraron canciones en las rutas configuradas";
        saveCache();
    }
}

void LibraryManager::updateScan(int maxTracksPerFrame) {
    if (!scanning_) return;
    if (maxTracksPerFrame < 1) maxTracksPerFrame = 1;

    for (int n = 0; n < maxTracksPerFrame && scan_index_ < pending_paths_.size(); ++n) {
        TrackMetadata item = MetadataReader::read(pending_paths_[scan_index_]);
        item.path = pending_paths_[scan_index_];
        scan_tracks_.push_back(item);
        ++scan_index_;
    }

    if (scan_index_ >= pending_paths_.size()) {
        tracks_.swap(scan_tracks_);
        pending_paths_.clear();
        scan_tracks_.clear();
        scan_index_ = 0;
        scanning_ = false;
        status_message_ = "Biblioteca actualizada: " + std::to_string(tracks_.size()) + " canciones";
        saveCache();
    }
}

const TrackMetadata* LibraryManager::findTrack(const std::string& path) const {
    for (const auto& item : tracks_) {
        if (item.path == path) return &item;
    }
    return nullptr;
}
