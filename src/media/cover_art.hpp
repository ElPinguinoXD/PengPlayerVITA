#pragma once

#include <string>
#include <vector>
#include <vita2d.h>

class CoverArt {
public:
    CoverArt() = default;
    ~CoverArt();

    CoverArt(const CoverArt&) = delete;
    CoverArt& operator=(const CoverArt&) = delete;

    bool loadForTrack(const std::string& audioPath, const std::string& customCoverPath = std::string());
    void clear();

    vita2d_texture* texture() const { return texture_; }
    bool hasTexture() const { return texture_ != nullptr; }
    const std::string& sourceLabel() const { return source_label_; }

private:
    bool loadEmbeddedMp3(const std::string& path);
    bool loadEmbeddedFlac(const std::string& path);
    bool loadFolderImage(const std::string& path);
    bool loadImageFile(const std::string& path);
    bool loadImageMemory(const std::vector<unsigned char>& data);

    vita2d_texture* texture_ = nullptr;
    std::string source_label_;
};
