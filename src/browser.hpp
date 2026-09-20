#pragma once

#include <string>
#include <vector>

struct FileEntry {
    std::string name;
    std::string path;
    bool isDirectory;
};

class MusicBrowser {
public:
    explicit MusicBrowser(const std::string& startPath);

    bool refresh();
    void moveUp();
    void moveDown();
    bool enterSelected(std::string& selectedAudioPath);
    bool goBack();
    void toggleSortDirection();

    const std::string& currentPath() const { return current_path_; }
    const std::vector<FileEntry>& entries() const { return entries_; }
    int selectedIndex() const { return selected_; }
    int scrollOffset() const { return scroll_; }
    bool ascending() const { return ascending_; }
    const std::string& lastError() const { return last_error_; }

private:
    static bool isAudioFile(const std::string& name);
    static std::string joinPath(const std::string& base, const std::string& name);
    static std::string parentPath(const std::string& path);
    void clampSelection();
    void sortEntries();

    std::string current_path_;
    std::vector<FileEntry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
    bool ascending_ = true;
    std::string last_error_;
};
