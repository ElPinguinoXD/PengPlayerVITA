#include "preferences_manager.hpp"

#include <psp2/io/stat.h>

#include <cstdio>
#include <cstdlib>

namespace {
const char* kDataDir = "ux0:data/PengPlayer";
const char* kSettingsPath = "ux0:data/PengPlayer/preferences.cfg";
const char* kCustomCoversPath = "ux0:data/PengPlayer/custom_covers.dat";

void ensureDataDir() {
    sceIoMkdir(kDataDir, 0777);
}

std::string trimLine(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}
}

void PreferencesManager::initialize() {
    ensureDataDir();
    loadSettings();
    loadCustomCovers();
}

void PreferencesManager::setAccentHue(int hue) {
    hue %= 360;
    if (hue < 0) hue += 360;
    accent_hue_ = hue;
    saveSettings();
}

void PreferencesManager::setVisualizerMode(int mode) {
    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    visualizer_mode_ = mode;
    saveSettings();
}

std::string PreferencesManager::customCoverFor(const std::string& trackPath) const {
    const auto it = custom_covers_.find(trackPath);
    return it == custom_covers_.end() ? std::string() : it->second;
}

bool PreferencesManager::setCustomCover(const std::string& trackPath, const std::string& imagePath) {
    if (trackPath.empty() || imagePath.empty()) return false;
    custom_covers_[trackPath] = imagePath;
    saveCustomCovers();
    return true;
}

bool PreferencesManager::removeCustomCover(const std::string& trackPath) {
    const auto it = custom_covers_.find(trackPath);
    if (it == custom_covers_.end()) return false;
    custom_covers_.erase(it);
    saveCustomCovers();
    return true;
}

bool PreferencesManager::hasCustomCover(const std::string& trackPath) const {
    return custom_covers_.find(trackPath) != custom_covers_.end();
}

void PreferencesManager::loadSettings() {
    // 259° mantiene el morado original de PengPlayer como valor por defecto.
    accent_hue_ = 259;
    int legacyAccent = -1;
    bool loadedHue = false;
    visualizer_mode_ = 0;

    std::FILE* file = std::fopen(kSettingsPath, "rb");
    if (!file) return;

    char line[128];
    while (std::fgets(line, sizeof(line), file)) {
        std::string value = trimLine(line);
        const std::string huePrefix = "accent_hue=";
        const std::string legacyPrefix = "accent=";
        const std::string visualizerPrefix = "visualizer_mode=";
        if (value.compare(0, huePrefix.size(), huePrefix) == 0) {
            accent_hue_ = std::atoi(value.substr(huePrefix.size()).c_str());
            accent_hue_ %= 360;
            if (accent_hue_ < 0) accent_hue_ += 360;
            loadedHue = true;
        } else if (value.compare(0, legacyPrefix.size(), legacyPrefix) == 0) {
            legacyAccent = std::atoi(value.substr(legacyPrefix.size()).c_str());
        } else if (value.compare(0, visualizerPrefix.size(), visualizerPrefix) == 0) {
            visualizer_mode_ = std::atoi(value.substr(visualizerPrefix.size()).c_str());
            if (visualizer_mode_ < 0 || visualizer_mode_ > 4) visualizer_mode_ = 0;
        }
    }
    std::fclose(file);

    // Migra silenciosamente los presets usados por la primera build 0.5.
    if (!loadedHue && legacyAccent >= 0) {
        static const int legacyHue[] = {259, 215, 173, 134, 29, 0, 324, 223};
        if (legacyAccent < static_cast<int>(sizeof(legacyHue) / sizeof(legacyHue[0])))
            accent_hue_ = legacyHue[legacyAccent];
    }
}

void PreferencesManager::saveSettings() const {
    ensureDataDir();
    std::FILE* file = std::fopen(kSettingsPath, "wb");
    if (!file) return;
    std::fprintf(file, "accent_hue=%d\n", accent_hue_);
    std::fprintf(file, "visualizer_mode=%d\n", visualizer_mode_);
    std::fclose(file);
}

void PreferencesManager::loadCustomCovers() {
    custom_covers_.clear();
    std::FILE* file = std::fopen(kCustomCoversPath, "rb");
    if (!file) return;

    char line[2048];
    while (std::fgets(line, sizeof(line), file)) {
        std::string value = trimLine(line);
        const std::size_t tab = value.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= value.size()) continue;
        custom_covers_[value.substr(0, tab)] = value.substr(tab + 1);
    }
    std::fclose(file);
}

void PreferencesManager::saveCustomCovers() const {
    ensureDataDir();
    std::FILE* file = std::fopen(kCustomCoversPath, "wb");
    if (!file) return;
    for (const auto& item : custom_covers_) {
        std::fprintf(file, "%s\t%s\n", item.first.c_str(), item.second.c_str());
    }
    std::fclose(file);
}
