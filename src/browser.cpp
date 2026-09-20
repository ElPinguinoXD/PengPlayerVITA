#include "browser.hpp"

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

MusicBrowser::MusicBrowser(const std::string& startPath)
    : current_path_(startPath) {
    refresh();
}

bool MusicBrowser::isAudioFile(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;

    const std::string ext = lowerCopy(name.substr(dot));
    static const char* kExtensions[] = {
        ".mp3", ".flac", ".wav", ".ogg", ".opus",
        ".aiff", ".aif"
    };

    for (const char* supported : kExtensions) {
        if (ext == supported) return true;
    }
    return false;
}

std::string MusicBrowser::joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

std::string MusicBrowser::parentPath(const std::string& path) {
    // Keep mount roots such as ux0:/ intact.
    if (path.size() <= 5 && path.find(":/") != std::string::npos) return path;

    std::string temp = path;
    while (temp.size() > 5 && temp.back() == '/') temp.pop_back();

    const std::size_t slash = temp.find_last_of('/');
    if (slash == std::string::npos) return temp;
    if (slash <= 4) return temp.substr(0, slash + 1);
    return temp.substr(0, slash);
}

bool MusicBrowser::refresh() {
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
        const int res = sceIoDread(dfd, &dir);
        if (res <= 0) break;

        const std::string name = dir.d_name;
        if (name == "." || name == "..") continue;

        const bool isDir = SCE_S_ISDIR(dir.d_stat.st_mode);
        if (!isDir && !isAudioFile(name)) continue;

        entries_.push_back({name, joinPath(current_path_, name), isDir});
    }

    sceIoDclose(dfd);
    sortEntries();
    clampSelection();
    return true;
}

void MusicBrowser::sortEntries() {
    std::sort(entries_.begin(), entries_.end(), [this](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        const std::string an = lowerCopy(a.name);
        const std::string bn = lowerCopy(b.name);
        return ascending_ ? (an < bn) : (an > bn);
    });
}

void MusicBrowser::clampSelection() {
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

void MusicBrowser::moveUp() {
    if (entries_.empty()) return;
    --selected_;
    if (selected_ < 0) selected_ = static_cast<int>(entries_.size()) - 1;
    clampSelection();
}

void MusicBrowser::moveDown() {
    if (entries_.empty()) return;
    ++selected_;
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = 0;
    clampSelection();
}

bool MusicBrowser::enterSelected(std::string& selectedAudioPath) {
    if (entries_.empty()) return false;

    const FileEntry entry = entries_[selected_];
    if (entry.isDirectory) {
        current_path_ = entry.path;
        selected_ = 0;
        scroll_ = 0;
        refresh();
        return false;
    }

    selectedAudioPath = entry.path;
    return true;
}

bool MusicBrowser::selectAdjacentAudio(int direction, std::string& selectedAudioPath) {
    if (entries_.empty() || direction == 0) return false;

    int base = selected_;
    if (!selectedAudioPath.empty()) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (!entries_[i].isDirectory && entries_[i].path == selectedAudioPath) {
                base = i;
                break;
            }
        }
    }

    const int count = static_cast<int>(entries_.size());
    for (int step = 1; step <= count; ++step) {
        int candidate = base + direction * step;
        while (candidate < 0) candidate += count;
        candidate %= count;

        if (!entries_[candidate].isDirectory) {
            selected_ = candidate;
            selectedAudioPath = entries_[candidate].path;
            clampSelection();
            return true;
        }
    }

    return false;
}

bool MusicBrowser::goBack() {
    const std::string parent = parentPath(current_path_);
    if (parent == current_path_) return false;

    current_path_ = parent;
    selected_ = 0;
    scroll_ = 0;
    refresh();
    return true;
}

void MusicBrowser::toggleSortDirection() {
    ascending_ = !ascending_;
    sortEntries();
    clampSelection();
}
