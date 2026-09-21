#include "image_browser.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}
}

ImageBrowser::ImageBrowser(const std::string& startPath)
    : current_path_(startPath) {
    refresh();
}

bool ImageBrowser::isImageFile(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerCopy(name.substr(dot));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

std::string ImageBrowser::joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

std::string ImageBrowser::parentPath(const std::string& path) {
    if (path.size() <= 5 && path.find(":/") != std::string::npos) return path;

    std::string temp = path;
    while (temp.size() > 5 && temp.back() == '/') temp.pop_back();

    const std::size_t slash = temp.find_last_of('/');
    if (slash == std::string::npos) return temp;
    if (slash <= 4) return temp.substr(0, slash + 1);
    return temp.substr(0, slash);
}

bool ImageBrowser::refresh() {
    entries_.clear();
    last_error_.clear();

    SceUID dfd = sceIoDopen(current_path_.c_str());
    if (dfd < 0) {
        last_error_ = "No se pudo abrir: " + current_path_;
        selected_ = 0;
        scroll_ = 0;
        return false;
    }

    while (true) {
        SceIoDirent dir;
        std::memset(&dir, 0, sizeof(dir));
        const int result = sceIoDread(dfd, &dir);
        if (result <= 0) break;

        const std::string name = dir.d_name;
        if (name == "." || name == "..") continue;

        const bool isDir = SCE_S_ISDIR(dir.d_stat.st_mode);
        if (!isDir && !isImageFile(name)) continue;
        entries_.push_back({name, joinPath(current_path_, name), isDir});
    }

    sceIoDclose(dfd);
    sortEntries();
    clampSelection();
    return true;
}

void ImageBrowser::sortEntries() {
    std::sort(entries_.begin(), entries_.end(), [](const ImageEntry& a, const ImageEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        return lowerCopy(a.name) < lowerCopy(b.name);
    });
}

void ImageBrowser::clampSelection() {
    if (entries_.empty()) {
        selected_ = 0;
        scroll_ = 0;
        return;
    }
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = static_cast<int>(entries_.size()) - 1;

    constexpr int kVisibleRows = 11;
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + kVisibleRows) scroll_ = selected_ - kVisibleRows + 1;
    if (scroll_ < 0) scroll_ = 0;
}

void ImageBrowser::moveUp() {
    if (entries_.empty()) return;
    --selected_;
    if (selected_ < 0) selected_ = static_cast<int>(entries_.size()) - 1;
    clampSelection();
}

void ImageBrowser::moveDown() {
    if (entries_.empty()) return;
    ++selected_;
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = 0;
    clampSelection();
}

bool ImageBrowser::enterSelected(std::string& selectedImagePath) {
    if (entries_.empty()) return false;
    const ImageEntry entry = entries_[selected_];
    if (entry.isDirectory) {
        current_path_ = entry.path;
        selected_ = 0;
        scroll_ = 0;
        refresh();
        return false;
    }
    selectedImagePath = entry.path;
    return true;
}

bool ImageBrowser::goBack() {
    const std::string parent = parentPath(current_path_);
    if (parent == current_path_) return false;
    current_path_ = parent;
    selected_ = 0;
    scroll_ = 0;
    refresh();
    return true;
}
