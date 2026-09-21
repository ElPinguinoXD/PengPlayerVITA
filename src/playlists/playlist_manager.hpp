#pragma once

#include <string>
#include <vector>

struct Playlist {
    std::string name;
    std::vector<std::string> tracks;
};

class PlaylistManager {
public:
    void initialize();

    const std::vector<Playlist>& playlists() const { return playlists_; }
    std::vector<std::string> names() const;
    const Playlist* find(const std::string& name) const;

    std::string createPlaylist(const std::string& requestedName);
    bool deletePlaylist(const std::string& name);
    bool addTrack(const std::string& playlistName, const std::string& path);
    bool removeTrack(const std::string& playlistName, std::size_t index);

private:
    void ensureDataDirectory();
    void load();
    void save() const;

    std::vector<Playlist> playlists_;
};
