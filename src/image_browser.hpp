#pragma once

#include <string>
#include <vector>

struct ImageEntry {
    std::string name;
    std::string path;
    bool isDirectory;
};

class ImageBrowser {
public:
    explicit ImageBrowser(const std::string& startPath);

    bool refresh();
    void moveUp();
    void moveDown();
    bool enterSelected(std::string& selectedImagePath);
    bool goBack();

    const std::string& currentPath() const { return current_path_; }
    const std::vector<ImageEntry>& entries() const { return entries_; }
    int selectedIndex() const { return selected_; }
    int scrollOffset() const { return scroll_; }
    const std::string& lastError() const { return last_error_; }

private:
    static bool isImageFile(const std::string& name);
    static std::string joinPath(const std::string& base, const std::string& name);
    static std::string parentPath(const std::string& path);
    void clampSelection();
    void sortEntries();

    std::string current_path_;
    std::vector<ImageEntry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
    std::string last_error_;
};
