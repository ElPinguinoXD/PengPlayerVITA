#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

struct SmartCategory {
    std::string name;
    std::vector<std::string> tracks;
};

struct SessionState {
    bool valid = false;
    std::string currentPath;
    int positionMs = 0;
    int currentIndex = -1;
    bool paused = true;
    std::vector<std::string> queue;
};

class SmartLibraryManager {
public:
    void initialize();

    bool isFavorite(const std::string& path) const;
    bool toggleFavorite(const std::string& path);
    std::vector<std::string> favoritePaths() const;

    void markPlayed(const std::string& path);
    int playCount(const std::string& path) const;
    const std::vector<std::string>& recentPaths() const { return recent_paths_; }
    std::vector<std::string> mostPlayedPaths() const;

    const std::vector<SmartCategory>& categories() const { return categories_; }
    const SmartCategory* findCategory(const std::string& name) const;
    std::string createCategory(const std::string& requestedName);
    bool deleteCategory(const std::string& name);
    bool addTrackToCategory(const std::string& categoryName, const std::string& path);
    bool removeTrackFromCategory(const std::string& categoryName, std::size_t index);

    void saveSession(const SessionState& state) const;
    SessionState loadSession() const;

private:
    void ensureDataDirectory() const;
    void loadData();
    void saveData() const;

    std::set<std::string> favorites_;
    std::map<std::string, int> play_counts_;
    std::vector<std::string> recent_paths_;
    std::vector<SmartCategory> categories_;
};
