// SPDX-License-Identifier: GPL-3.0-or-later
#include "../mobile/TemporaryFiles.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc, char** argv) {
    assert(argc == 2);
    namespace fs = std::filesystem;
    const fs::path root = argv[1];
    fs::create_directories(root / "profile/recovery");
    fs::create_directories(root / "outside");
    const pid_t child = fork(); assert(child >= 0);
    if (!child) _exit(0);
    int status; assert(waitpid(child, &status, 0) == child);
    const std::string dead = "slot0.glob2-tmp-" + std::to_string(child) + "-42";
    const std::string alive = "slot0.glob2-tmp-" + std::to_string(getpid()) + "-42";
    auto file = [](const fs::path& path) { std::ofstream(path) << "retained bytes"; };
    file(root / "profile/recovery" / dead);
    file(root / "profile/recovery" / alive);
    file(root / "profile/recovery/slot0");
    file(root / "profile/recovery/state");
    file(root / "profile/recovery/slot1.tmp-1-0"); // Ambiguous legacy names are retained.
    file(root / "profile/recovery/bad.glob2-tmp-999999999999999999999-0");
    file(root / "outside" / dead);
    fs::create_directory_symlink(root / "outside", root / "profile/linked-directory");
    fs::create_symlink(root / "outside" / dead, root / "profile" / dead);
    assert(MobileTemporaryFiles::cleanup((root / "profile").string()) == 1);
    assert(!fs::exists(root / "profile/recovery" / dead));
    assert(fs::exists(root / "profile/recovery" / alive));
    assert(fs::exists(root / "profile/recovery/slot0"));
    assert(fs::exists(root / "profile/recovery/state"));
    assert(fs::exists(root / "profile/recovery/slot1.tmp-1-0"));
    assert(fs::exists(root / "outside" / dead));
    assert(fs::is_symlink(root / "profile" / dead));
    file(root / "profile/recovery" / dead);
    int locked = open((root / "profile/recovery" / dead).c_str(), O_RDWR); assert(locked >= 0);
    assert(flock(locked, LOCK_EX | LOCK_NB) == 0);
    assert(MobileTemporaryFiles::cleanup((root / "profile").string()) == 0);
    close(locked);
    assert(MobileTemporaryFiles::cleanup((root / "profile").string()) == 1);
    const std::string suffix = "-01234567-89ab-cdef-0123-456789abcdef";
    const fs::path abandoned = root / ("Glob2-export-" + std::to_string(child) + suffix);
    const fs::path active = root / ("Glob2-export-" + std::to_string(getpid()) + suffix);
    fs::create_directories(abandoned); fs::create_directories(active);
    file(abandoned / "export.game"); file(active / "export.game");
    fs::create_symlink(root / "outside" / dead, abandoned / "linked.game");
    assert(MobileTemporaryFiles::cleanupExports(root.string()) == 1);
    assert(!fs::exists(abandoned) && fs::exists(active / "export.game"));
    assert(fs::exists(root / "outside" / dead));

}
