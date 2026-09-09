// SPDX-License-Identifier: GPL-3.0-or-later
#include "TemporaryFiles.h"
#include <charconv>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace MobileTemporaryFiles {
namespace {
struct Descriptor {
    int value;
    ~Descriptor() { if (value >= 0) close(value); }
};
pid_t owner(std::string_view name) {
    constexpr std::string_view marker = ".glob2-tmp-";
    const auto position = name.rfind(marker);
    if (position == std::string_view::npos || position == 0) return 0;
    name.remove_prefix(position + marker.size());
    const auto separator = name.find('-');
    if (separator == std::string_view::npos) return 0;
    unsigned long long process = 0, sequence = 0;
    const auto pid = std::from_chars(name.data(), name.data() + separator, process);
    const auto suffix = std::from_chars(name.data() + separator + 1, name.data() + name.size(), sequence);
    if (pid.ec != std::errc{} || pid.ptr != name.data() + separator ||
        suffix.ec != std::errc{} || suffix.ptr != name.data() + name.size() ||
        !process || process > static_cast<unsigned long long>(std::numeric_limits<pid_t>::max())) return 0;
    return static_cast<pid_t>(process);
}
std::size_t walk(int directory, unsigned depth, unsigned& budget) {
    Descriptor copy{dup(directory)};
    if (copy.value < 0) return 0;
    DIR* entries = fdopendir(copy.value);
    if (!entries) return 0;
    copy.value = -1;
    std::size_t removed = 0;
    while (budget) {
        auto* entry = readdir(entries);
        if (!entry) break;
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        --budget;
        struct stat information{};
        if (fstatat(directory, name.c_str(), &information, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (S_ISDIR(information.st_mode)) {
            if (depth >= 8 || (depth == 0 && name == "assets")) continue;
            Descriptor child{openat(directory, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            if (child.value >= 0) removed += walk(child.value, depth + 1, budget);
            continue;
        }
        const pid_t process = owner(name);
        if (!process || !S_ISREG(information.st_mode)) continue;
        Descriptor file{openat(directory, name.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC)};
        if (file.value < 0 || flock(file.value, LOCK_EX | LOCK_NB) != 0) continue;
        struct stat opened{}, current{};
        if (fstat(file.value, &opened) != 0 || fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
            opened.st_dev != current.st_dev || opened.st_ino != current.st_ino) continue;
        // EPERM and every result other than ESRCH mean alive or uncertain.
        // Exclusive cleanup locks plus identity checks prevent competing cleanup
        // passes from unlinking a newly created file after PID reuse.
        if (kill(process, 0) != -1 || errno != ESRCH) continue;
        if (unlinkat(directory, name.c_str(), 0) == 0) ++removed;
    }
    closedir(entries);
    return removed;
}
}
std::size_t cleanup(const std::string& profile) {
    Descriptor directory{open(profile.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (directory.value < 0) return 0;
    unsigned budget = 4096;
    return walk(directory.value, 0, budget);
}
std::size_t cleanupExports(const std::string& temporaryDirectory) {
    Descriptor root{open(temporaryDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (root.value < 0) return 0;
    Descriptor copy{dup(root.value)};
    DIR* entries = copy.value >= 0 ? fdopendir(copy.value) : nullptr;
    if (!entries) return 0;
    copy.value = -1;
    std::size_t removed = 0;
    unsigned budget = 1024;
    while (budget--) {
        auto* entry = readdir(entries);
        if (!entry) break;
        std::string_view name(entry->d_name);
        constexpr std::string_view prefix = "Glob2-export-";
        if (!name.starts_with(prefix)) continue;
        const auto separator = name.find('-', prefix.size());
        if (separator == std::string_view::npos || name.size() - separator - 1 != 36) continue;
        unsigned long long process = 0;
        auto parsed = std::from_chars(name.data() + prefix.size(), name.data() + separator, process);
        if (parsed.ec != std::errc{} || parsed.ptr != name.data() + separator || !process ||
            process > static_cast<unsigned long long>(std::numeric_limits<pid_t>::max())) continue;
        const auto uuid = name.substr(separator + 1);
        bool valid = true;
        for (unsigned i = 0; i < uuid.size(); ++i) {
            const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
            valid &= dash ? uuid[i] == '-' : std::string_view("0123456789abcdefABCDEF").find(uuid[i]) != std::string_view::npos;
        }
        if (!valid) continue;
        Descriptor directory{openat(root.value, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (directory.value < 0 || flock(directory.value, LOCK_EX | LOCK_NB) != 0 ||
            kill(static_cast<pid_t>(process), 0) != -1 || errno != ESRCH) continue;
        Descriptor childCopy{dup(directory.value)};
        DIR* contents = childCopy.value >= 0 ? fdopendir(childCopy.value) : nullptr;
        if (!contents) continue;
        childCopy.value = -1;
        unsigned files = 32;
        while (files--) {
            auto* child = readdir(contents);
            if (!child) break;
            if (std::string_view(child->d_name) == "." || std::string_view(child->d_name) == "..") continue;
            // Export staging contains a single file. Unexpected subdirectories
            // are retained; unlinkat without AT_REMOVEDIR never traverses them.
            unlinkat(directory.value, child->d_name, 0);
        }
        closedir(contents);
        struct stat held{}, current{};
        if (fstat(directory.value, &held) == 0 && fstatat(root.value, entry->d_name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            held.st_dev == current.st_dev && held.st_ino == current.st_ino &&
            unlinkat(root.value, entry->d_name, AT_REMOVEDIR) == 0) ++removed;
    }
    closedir(entries);
    return removed;
}

}
