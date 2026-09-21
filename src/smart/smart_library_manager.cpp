#include "smart_library_manager.hpp"

#include <psp2/io/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
const char* kDataDir = "ux0:data/PengPlayer";
const char* kSmartPath = "ux0:data/PengPlayer/smart_library.dat";
const char* kSessionPath = "ux0:data/PengPlayer/session.dat";

std::string trimLine(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

std::string sanitizeName(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    const std::size_t first = value.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(' ');
    return value.substr(first, last - first + 1);
}
}

void SmartLibraryManager::initialize() {
    ensureDataDirectory();
    loadData();
}

void SmartLibraryManager::ensureDataDirectory() const {
    sceIoMkdir(kDataDir, 0777);
}

bool SmartLibraryManager::isFavorite(const std::string& path) const {
    return favorites_.find(path) != favorites_.end();
}

bool SmartLibraryManager::toggleFavorite(const std::string& path) {
    if (path.empty()) return false;
    auto it = favorites_.find(path);
    if (it == favorites_.end()) favorites_.insert(path);
    else favorites_.erase(it);
    saveData();
    return isFavorite(path);
}

std::vector<std::string> SmartLibraryManager::favoritePaths() const {
    return std::vector<std::string>(favorites_.begin(), favorites_.end());
}

void SmartLibraryManager::markPlayed(const std::string& path) {
    if (path.empty()) return;
    ++play_counts_[path];
    auto it = std::find(recent_paths_.begin(), recent_paths_.end(), path);
    if (it != recent_paths_.end()) recent_paths_.erase(it);
    recent_paths_.insert(recent_paths_.begin(), path);
    if (recent_paths_.size() > 50) recent_paths_.resize(50);
    saveData();
}

int SmartLibraryManager::playCount(const std::string& path) const {
    const auto it = play_counts_.find(path);
    return it == play_counts_.end() ? 0 : it->second;
}

std::vector<std::string> SmartLibraryManager::mostPlayedPaths() const {
    std::vector<std::pair<std::string, int>> values;
    values.reserve(play_counts_.size());
    for (const auto& item : play_counts_) {
        if (item.second > 0) values.push_back(item);
    }
    std::sort(values.begin(), values.end(), [](const std::pair<std::string, int>& a,
                                                const std::pair<std::string, int>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& item : values) result.push_back(item.first);
    return result;
}

const SmartCategory* SmartLibraryManager::findCategory(const std::string& name) const {
    for (const auto& category : categories_) {
        if (category.name == name) return &category;
    }
    return nullptr;
}

std::string SmartLibraryManager::createCategory(const std::string& requestedName) {
    const std::string base = sanitizeName(requestedName);
    if (base.empty()) return "";

    std::string candidate = base;
    int suffix = 2;
    while (findCategory(candidate)) candidate = base + " (" + std::to_string(suffix++) + ")";

    categories_.push_back({candidate, {}});
    saveData();
    return candidate;
}

bool SmartLibraryManager::deleteCategory(const std::string& name) {
    auto it = std::find_if(categories_.begin(), categories_.end(), [&](const SmartCategory& category) {
        return category.name == name;
    });
    if (it == categories_.end()) return false;
    categories_.erase(it);
    saveData();
    return true;
}

bool SmartLibraryManager::addTrackToCategory(const std::string& categoryName, const std::string& path) {
    if (path.empty()) return false;
    for (auto& category : categories_) {
        if (category.name != categoryName) continue;
        if (std::find(category.tracks.begin(), category.tracks.end(), path) == category.tracks.end())
            category.tracks.push_back(path);
        saveData();
        return true;
    }
    return false;
}

bool SmartLibraryManager::removeTrackFromCategory(const std::string& categoryName, std::size_t index) {
    for (auto& category : categories_) {
        if (category.name != categoryName) continue;
        if (index >= category.tracks.size()) return false;
        category.tracks.erase(category.tracks.begin() + static_cast<long>(index));
        saveData();
        return true;
    }
    return false;
}

void SmartLibraryManager::loadData() {
    favorites_.clear();
    play_counts_.clear();
    recent_paths_.clear();
    categories_.clear();

    std::FILE* file = std::fopen(kSmartPath, "rb");
    if (!file) return;

    SmartCategory* currentCategory = nullptr;
    char line[4096];
    while (std::fgets(line, sizeof(line), file)) {
        const std::string value = trimLine(line);
        if (value.size() > 2 && value[0] == 'F' && value[1] == '\t') {
            favorites_.insert(value.substr(2));
            currentCategory = nullptr;
        } else if (value.size() > 2 && value[0] == 'P' && value[1] == '\t') {
            const std::size_t tab = value.find('\t', 2);
            if (tab != std::string::npos) {
                const std::string path = value.substr(2, tab - 2);
                const int count = std::max(0, std::atoi(value.substr(tab + 1).c_str()));
                if (!path.empty() && count > 0) play_counts_[path] = count;
            }
            currentCategory = nullptr;
        } else if (value.size() > 2 && value[0] == 'R' && value[1] == '\t') {
            const std::string path = value.substr(2);
            if (!path.empty()) recent_paths_.push_back(path);
            currentCategory = nullptr;
        } else if (value.size() > 2 && value[0] == 'C' && value[1] == '\t') {
            categories_.push_back({value.substr(2), {}});
            currentCategory = &categories_.back();
        } else if (currentCategory && value.size() > 2 && value[0] == 'T' && value[1] == '\t') {
            currentCategory->tracks.push_back(value.substr(2));
        }
    }
    std::fclose(file);

    if (recent_paths_.size() > 50) recent_paths_.resize(50);
}

void SmartLibraryManager::saveData() const {
    ensureDataDirectory();
    std::FILE* file = std::fopen(kSmartPath, "wb");
    if (!file) return;

    for (const auto& path : favorites_) std::fprintf(file, "F\t%s\n", path.c_str());
    for (const auto& item : play_counts_) std::fprintf(file, "P\t%s\t%d\n", item.first.c_str(), item.second);
    for (const auto& path : recent_paths_) std::fprintf(file, "R\t%s\n", path.c_str());
    for (const auto& category : categories_) {
        std::fprintf(file, "C\t%s\n", category.name.c_str());
        for (const auto& path : category.tracks) std::fprintf(file, "T\t%s\n", path.c_str());
    }
    std::fclose(file);
}

void SmartLibraryManager::saveSession(const SessionState& state) const {
    ensureDataDirectory();
    std::FILE* file = std::fopen(kSessionPath, "wb");
    if (!file) return;

    std::fprintf(file, "V\t1\n");
    std::fprintf(file, "C\t%s\n", state.currentPath.c_str());
    std::fprintf(file, "M\t%d\n", std::max(0, state.positionMs));
    std::fprintf(file, "I\t%d\n", state.currentIndex);
    std::fprintf(file, "A\t%d\n", state.paused ? 1 : 0);
    for (const auto& path : state.queue) std::fprintf(file, "Q\t%s\n", path.c_str());
    std::fclose(file);
}

SessionState SmartLibraryManager::loadSession() const {
    SessionState state;
    std::FILE* file = std::fopen(kSessionPath, "rb");
    if (!file) return state;

    char line[4096];
    while (std::fgets(line, sizeof(line), file)) {
        const std::string value = trimLine(line);
        if (value.size() > 2 && value[0] == 'C' && value[1] == '\t') {
            state.currentPath = value.substr(2);
        } else if (value.size() > 2 && value[0] == 'M' && value[1] == '\t') {
            state.positionMs = std::max(0, std::atoi(value.substr(2).c_str()));
        } else if (value.size() > 2 && value[0] == 'I' && value[1] == '\t') {
            state.currentIndex = std::atoi(value.substr(2).c_str());
        } else if (value.size() > 2 && value[0] == 'A' && value[1] == '\t') {
            state.paused = std::atoi(value.substr(2).c_str()) != 0;
        } else if (value.size() > 2 && value[0] == 'Q' && value[1] == '\t') {
            state.queue.push_back(value.substr(2));
        }
    }
    std::fclose(file);

    state.valid = !state.currentPath.empty();
    if (state.currentIndex < 0 || state.currentIndex >= static_cast<int>(state.queue.size())) {
        auto it = std::find(state.queue.begin(), state.queue.end(), state.currentPath);
        state.currentIndex = it == state.queue.end() ? -1 : static_cast<int>(it - state.queue.begin());
    }
    return state;
}
