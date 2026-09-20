#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "media/metadata.hpp"

class LibraryManager {
public:
    LibraryManager();

    void initialize();

    const std::vector<TrackMetadata>& tracks() const { return tracks_; }
    const std::vector<std::string>& roots() const { return roots_; }

    bool addRoot(const std::string& path);
    bool removeRoot(std::size_t index);

    void startScan();
    void updateScan(int maxTracksPerFrame = 1);
    bool isScanning() const { return scanning_; }
    int scanProcessed() const { return static_cast<int>(scan_index_); }
    int scanTotal() const { return static_cast<int>(pending_paths_.size()); }

    const TrackMetadata* findTrack(const std::string& path) const;
    const std::string& statusMessage() const { return status_message_; }

private:
    static bool isAudioFile(const std::string& name);
    static std::string normalizeRoot(const std::string& path);
    static std::string joinPath(const std::string& base, const std::string& name);

    void ensureDataDirectory();
    void loadRoots();
    void saveRoots() const;
    bool loadCache();
    void saveCache() const;
    void collectAudioRecursive(const std::string& path, int depth);

    std::vector<std::string> roots_;
    std::vector<TrackMetadata> tracks_;

    bool scanning_ = false;
    std::vector<std::string> pending_paths_;
    std::vector<TrackMetadata> scan_tracks_;
    std::size_t scan_index_ = 0;
    std::string status_message_;
};
