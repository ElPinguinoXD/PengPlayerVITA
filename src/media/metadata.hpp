#pragma once

#include <string>

struct TrackMetadata {
    bool valid = false;
    std::string path;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string date;
    std::string trackNumber;
    std::string formatName;

    int durationMs = 0;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    int bitrateKbps = 0;
};

class MetadataReader {
public:
    static TrackMetadata read(const std::string& path);
};
