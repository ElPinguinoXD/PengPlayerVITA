#pragma once

#include <map>
#include <string>

class PreferencesManager {
public:
    void initialize();

    int accentHue() const { return accent_hue_; }
    void setAccentHue(int hue);

    int visualizerMode() const { return visualizer_mode_; }
    void setVisualizerMode(int mode);

    std::string customCoverFor(const std::string& trackPath) const;
    bool setCustomCover(const std::string& trackPath, const std::string& imagePath);
    bool removeCustomCover(const std::string& trackPath);
    bool hasCustomCover(const std::string& trackPath) const;

private:
    void loadSettings();
    void saveSettings() const;
    void loadCustomCovers();
    void saveCustomCovers() const;

    int accent_hue_ = 259;
    int visualizer_mode_ = 0;
    std::map<std::string, std::string> custom_covers_;
};
