#include "cover_art.hpp"

#include <psp2/io/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extensionOf(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    return lowerCopy(path.substr(dot + 1));
}

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

bool exists(const std::string& path) {
    SceIoStat stat{};
    return sceIoGetstat(path.c_str(), &stat) >= 0 && !SCE_S_ISDIR(stat.st_mode);
}

unsigned int be24(const unsigned char* p) {
    return (static_cast<unsigned int>(p[0]) << 16) |
           (static_cast<unsigned int>(p[1]) << 8) |
           static_cast<unsigned int>(p[2]);
}

unsigned int be32(const unsigned char* p) {
    return (static_cast<unsigned int>(p[0]) << 24) |
           (static_cast<unsigned int>(p[1]) << 16) |
           (static_cast<unsigned int>(p[2]) << 8) |
           static_cast<unsigned int>(p[3]);
}

unsigned int synchsafe32(const unsigned char* p) {
    return (static_cast<unsigned int>(p[0] & 0x7F) << 21) |
           (static_cast<unsigned int>(p[1] & 0x7F) << 14) |
           (static_cast<unsigned int>(p[2] & 0x7F) << 7) |
           static_cast<unsigned int>(p[3] & 0x7F);
}

bool isJpeg(const unsigned char* data, std::size_t size) {
    return size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool isPng(const unsigned char* data, std::size_t size) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return size >= 8 && std::memcmp(data, sig, 8) == 0;
}

std::size_t findImageSignature(const unsigned char* data, std::size_t size) {
    for (std::size_t i = 0; i + 8 <= size; ++i) {
        if (isJpeg(data + i, size - i) || isPng(data + i, size - i)) return i;
    }
    return size;
}

bool validFrameId(const unsigned char* p) {
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = p[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}
}

CoverArt::~CoverArt() {
    clear();
}

void CoverArt::clear() {
    if (texture_) {
        vita2d_free_texture(texture_);
        texture_ = nullptr;
    }
    source_label_.clear();
}

bool CoverArt::loadImageMemory(const std::vector<unsigned char>& data) {
    if (data.empty()) return false;

    vita2d_texture* loaded = nullptr;
    if (isJpeg(data.data(), data.size())) {
        loaded = vita2d_load_JPEG_buffer(data.data(), static_cast<unsigned long>(data.size()));
    } else if (isPng(data.data(), data.size())) {
        // libvita2d's PNG loader reads until the PNG IEND chunk.
        loaded = vita2d_load_PNG_buffer(data.data());
    }

    if (!loaded) return false;
    texture_ = loaded;
    vita2d_texture_set_filters(texture_, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return true;
}

bool CoverArt::loadImageFile(const std::string& path) {
    vita2d_texture* loaded = nullptr;
    const std::string ext = extensionOf(path);

    if (ext == "jpg" || ext == "jpeg") {
        loaded = vita2d_load_JPEG_file(path.c_str());
    } else if (ext == "png") {
        loaded = vita2d_load_PNG_file(path.c_str());
    }

    if (!loaded) return false;
    texture_ = loaded;
    vita2d_texture_set_filters(texture_, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    source_label_ = path.substr(path.find_last_of('/') + 1);
    return true;
}

bool CoverArt::loadEmbeddedMp3(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;

    unsigned char header[10]{};
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
        std::memcmp(header, "ID3", 3) != 0) {
        std::fclose(file);
        return false;
    }

    const int version = header[3];
    if (version != 3 && version != 4) {
        std::fclose(file);
        return false;
    }

    const unsigned int tagSize = synchsafe32(header + 6);
    if (tagSize == 0 || tagSize > 24U * 1024U * 1024U) {
        std::fclose(file);
        return false;
    }

    std::vector<unsigned char> tag(tagSize);
    if (std::fread(tag.data(), 1, tag.size(), file) != tag.size()) {
        std::fclose(file);
        return false;
    }
    std::fclose(file);

    std::size_t offset = 0;
    if ((header[5] & 0x40) && tag.size() >= 4) {
        const unsigned int extSize = version == 4 ? synchsafe32(tag.data()) : be32(tag.data());
        const std::size_t skip = version == 4 ? extSize : static_cast<std::size_t>(extSize) + 4;
        if (skip < tag.size()) offset = skip;
    }

    while (offset + 10 <= tag.size()) {
        const unsigned char* frame = tag.data() + offset;
        if (!validFrameId(frame)) break;

        const std::string id(reinterpret_cast<const char*>(frame), 4);
        const unsigned int frameSize = version == 4 ? synchsafe32(frame + 4) : be32(frame + 4);
        offset += 10;
        if (frameSize == 0 || offset + frameSize > tag.size()) break;

        if (id == "APIC") {
            const unsigned char* payload = tag.data() + offset;
            const std::size_t imageOffset = findImageSignature(payload, frameSize);
            if (imageOffset < frameSize) {
                std::vector<unsigned char> image(payload + imageOffset, payload + frameSize);
                // A little padding makes the no-size PNG memory loader safer around its final read.
                image.resize(image.size() + 16, 0);
                if (loadImageMemory(image)) {
                    source_label_ = "Caratula embebida";
                    return true;
                }
            }
        }

        offset += frameSize;
    }

    return false;
}

bool CoverArt::loadEmbeddedFlac(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;

    unsigned char magic[4]{};
    if (std::fread(magic, 1, 4, file) != 4 || std::memcmp(magic, "fLaC", 4) != 0) {
        std::fclose(file);
        return false;
    }

    bool last = false;
    while (!last) {
        unsigned char blockHeader[4]{};
        if (std::fread(blockHeader, 1, 4, file) != 4) break;

        last = (blockHeader[0] & 0x80) != 0;
        const int type = blockHeader[0] & 0x7F;
        const unsigned int length = be24(blockHeader + 1);
        if (length > 24U * 1024U * 1024U) break;

        if (type != 6) {
            if (std::fseek(file, static_cast<long>(length), SEEK_CUR) != 0) break;
            continue;
        }

        std::vector<unsigned char> block(length);
        if (length == 0 || std::fread(block.data(), 1, block.size(), file) != block.size()) break;

        std::size_t pos = 0;
        if (pos + 4 > block.size()) continue; // picture type
        pos += 4;

        if (pos + 4 > block.size()) continue;
        const unsigned int mimeLen = be32(block.data() + pos);
        pos += 4;
        if (pos + mimeLen > block.size()) continue;
        pos += mimeLen;

        if (pos + 4 > block.size()) continue;
        const unsigned int descLen = be32(block.data() + pos);
        pos += 4;
        if (pos + descLen > block.size()) continue;
        pos += descLen;

        if (pos + 20 > block.size()) continue; // width, height, depth, colors, data length
        pos += 16;
        const unsigned int dataLen = be32(block.data() + pos);
        pos += 4;
        if (dataLen == 0 || pos + dataLen > block.size()) continue;

        std::vector<unsigned char> image(block.begin() + pos, block.begin() + pos + dataLen);
        image.resize(image.size() + 16, 0);
        if (loadImageMemory(image)) {
            source_label_ = "Caratula embebida";
            std::fclose(file);
            return true;
        }
    }

    std::fclose(file);
    return false;
}

bool CoverArt::loadFolderImage(const std::string& path) {
    const std::string directory = directoryOf(path);
    static const char* kCandidates[] = {
        "cover.jpg", "cover.jpeg", "cover.png",
        "folder.jpg", "folder.jpeg", "folder.png",
        "front.jpg", "front.jpeg", "front.png",
        "album.jpg", "album.jpeg", "album.png"
    };

    for (const char* name : kCandidates) {
        const std::string candidate = joinPath(directory, name);
        if (exists(candidate) && loadImageFile(candidate)) return true;
    }
    return false;
}

bool CoverArt::loadForTrack(const std::string& audioPath, const std::string& customCoverPath) {
    clear();

    if (!customCoverPath.empty() && exists(customCoverPath) && loadImageFile(customCoverPath)) {
        source_label_ = "Caratula personalizada";
        return true;
    }

    const std::string ext = extensionOf(audioPath);
    if (ext == "mp3" && loadEmbeddedMp3(audioPath)) return true;
    if (ext == "flac" && loadEmbeddedFlac(audioPath)) return true;
    if (loadFolderImage(audioPath)) return true;

    return false;
}
