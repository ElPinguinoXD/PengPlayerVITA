#include "metadata.hpp"

#include <psp2/io/stat.h>
#include <sndfile.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extensionOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lowerCopy(path.substr(dot + 1));
}

std::string filenameStem(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
    const std::size_t dot = path.find_last_of('.');
    const std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
    return path.substr(begin, end - begin);
}

std::string tagString(SNDFILE* file, int type) {
    const char* value = sf_get_string(file, type);
    return value ? std::string(value) : std::string();
}

std::string formatNameFromExtension(const std::string& ext) {
    if (ext == "mp3") return "MP3";
    if (ext == "flac") return "FLAC";
    if (ext == "wav") return "WAV";
    if (ext == "ogg") return "OGG Vorbis";
    if (ext == "opus") return "Opus";
    if (ext == "aiff" || ext == "aif") return "AIFF";
    return ext.empty() ? "Audio" : ext;
}

int bitDepthFromSubtype(int subtype) {
    switch (subtype) {
        case SF_FORMAT_PCM_S8:
        case SF_FORMAT_PCM_U8:
            return 8;
        case SF_FORMAT_PCM_16:
            return 16;
        case SF_FORMAT_PCM_24:
            return 24;
        case SF_FORMAT_PCM_32:
        case SF_FORMAT_FLOAT:
            return 32;
        case SF_FORMAT_DOUBLE:
            return 64;
        default:
            return 0;
    }
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

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string latin1ToUtf8(const unsigned char* data, std::size_t size) {
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] == 0) break;
        appendUtf8(out, data[i]);
    }
    return out;
}

std::string utf16ToUtf8(const unsigned char* data, std::size_t size, bool bigEndian) {
    std::string out;
    std::size_t i = 0;

    while (i + 1 < size) {
        unsigned int unit = bigEndian
            ? (static_cast<unsigned int>(data[i]) << 8) | data[i + 1]
            : (static_cast<unsigned int>(data[i + 1]) << 8) | data[i];
        i += 2;
        if (unit == 0) break;

        unsigned int cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < size) {
            const unsigned int low = bigEndian
                ? (static_cast<unsigned int>(data[i]) << 8) | data[i + 1]
                : (static_cast<unsigned int>(data[i + 1]) << 8) | data[i];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                i += 2;
                cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

std::string decodeId3Text(const unsigned char* data, std::size_t size) {
    if (!data || size < 2) return {};
    const unsigned char encoding = data[0];
    const unsigned char* text = data + 1;
    std::size_t textSize = size - 1;

    if (encoding == 0) {
        return latin1ToUtf8(text, textSize);
    }
    if (encoding == 3) {
        std::size_t end = 0;
        while (end < textSize && text[end] != 0) ++end;
        return std::string(reinterpret_cast<const char*>(text), end);
    }
    if (encoding == 1) {
        if (textSize >= 2 && text[0] == 0xFF && text[1] == 0xFE) {
            return utf16ToUtf8(text + 2, textSize - 2, false);
        }
        if (textSize >= 2 && text[0] == 0xFE && text[1] == 0xFF) {
            return utf16ToUtf8(text + 2, textSize - 2, true);
        }
        return utf16ToUtf8(text, textSize, false);
    }
    if (encoding == 2) {
        return utf16ToUtf8(text, textSize, true);
    }
    return {};
}

bool validFrameId(const unsigned char* p) {
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = p[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

void parseId3v2TextFallback(const std::string& path, TrackMetadata& out) {
    if (!out.title.empty() && !out.artist.empty() && !out.album.empty()) return;

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return;

    unsigned char header[10]{};
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
        std::memcmp(header, "ID3", 3) != 0) {
        std::fclose(file);
        return;
    }

    const int version = header[3];
    if (version != 3 && version != 4) {
        std::fclose(file);
        return;
    }

    const unsigned int tagSize = synchsafe32(header + 6);
    if (tagSize == 0 || tagSize > 16U * 1024U * 1024U) {
        std::fclose(file);
        return;
    }

    std::vector<unsigned char> tag(tagSize);
    if (std::fread(tag.data(), 1, tag.size(), file) != tag.size()) {
        std::fclose(file);
        return;
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

        const std::string value = decodeId3Text(tag.data() + offset, frameSize);
        if (!value.empty()) {
            if (id == "TIT2" && out.title.empty()) out.title = value;
            else if (id == "TPE1" && out.artist.empty()) out.artist = value;
            else if (id == "TALB" && out.album.empty()) out.album = value;
            else if (id == "TCON" && out.genre.empty()) out.genre = value;
            else if ((id == "TDRC" || id == "TYER") && out.date.empty()) out.date = value;
            else if (id == "TRCK" && out.trackNumber.empty()) out.trackNumber = value;
        }

        offset += frameSize;
    }
}
}

TrackMetadata MetadataReader::read(const std::string& path) {
    TrackMetadata out;
    out.path = path;
    out.formatName = formatNameFromExtension(extensionOf(path));

    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);

    if (file) {
        out.valid = true;
        out.title = tagString(file, SF_STR_TITLE);
        out.artist = tagString(file, SF_STR_ARTIST);
        out.album = tagString(file, SF_STR_ALBUM);
        out.genre = tagString(file, SF_STR_GENRE);
        out.date = tagString(file, SF_STR_DATE);
        out.trackNumber = tagString(file, SF_STR_TRACKNUMBER);

        out.sampleRate = info.samplerate;
        out.channels = info.channels;
        out.bitDepth = bitDepthFromSubtype(info.format & SF_FORMAT_SUBMASK);

        if (info.frames > 0 && info.samplerate > 0) {
            out.durationMs = static_cast<int>(
                (static_cast<long long>(info.frames) * 1000LL) / info.samplerate
            );
        }

        const int bytesPerSecond = sf_current_byterate(file);
        if (bytesPerSecond > 0) out.bitrateKbps = (bytesPerSecond * 8) / 1000;
        sf_close(file);
    }

    if (extensionOf(path) == "mp3") {
        parseId3v2TextFallback(path, out);
    }

    if (out.durationMs > 0 && out.bitrateKbps <= 0) {
        SceIoStat stat{};
        if (sceIoGetstat(path.c_str(), &stat) >= 0 && stat.st_size > 0) {
            out.bitrateKbps = static_cast<int>(
                (static_cast<long long>(stat.st_size) * 8LL) / out.durationMs
            );
        }
    }

    if (out.title.empty()) out.title = filenameStem(path);
    if (out.artist.empty()) out.artist = "Artista desconocido";
    if (out.album.empty()) out.album = "Album desconocido";

    return out;
}
