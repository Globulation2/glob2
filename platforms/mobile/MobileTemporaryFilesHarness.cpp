// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "../mobile/TemporaryFiles.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
TEST_SUITE("MobileTemporaryFiles")
{
TEST_CASE("cleanup removes only abandoned temporaries and exports")
{
    namespace fs = std::filesystem;
    glob2test::TempDir scratch("mobile-temporary");
    const fs::path root = scratch.path;
    fs::create_directories(root / "profile/recovery");
    fs::create_directories(root / "outside");
    const pid_t child = fork(); REQUIRE(child >= 0);
    if (!child) _exit(0);
    int status; REQUIRE(waitpid(child, &status, 0) == child);
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
    REQUIRE(MobileTemporaryFiles::cleanup((root / "profile").string()) == 1);
    REQUIRE(!fs::exists(root / "profile/recovery" / dead));
    REQUIRE(fs::exists(root / "profile/recovery" / alive));
    REQUIRE(fs::exists(root / "profile/recovery/slot0"));
    REQUIRE(fs::exists(root / "profile/recovery/state"));
    REQUIRE(fs::exists(root / "profile/recovery/slot1.tmp-1-0"));
    REQUIRE(fs::exists(root / "outside" / dead));
    REQUIRE(fs::is_symlink(root / "profile" / dead));
    file(root / "profile/recovery" / dead);
    int locked = open((root / "profile/recovery" / dead).c_str(), O_RDWR); REQUIRE(locked >= 0);
    REQUIRE(flock(locked, LOCK_EX | LOCK_NB) == 0);
    REQUIRE(MobileTemporaryFiles::cleanup((root / "profile").string()) == 0);
    close(locked);
    REQUIRE(MobileTemporaryFiles::cleanup((root / "profile").string()) == 1);
    const std::string suffix = "-01234567-89ab-cdef-0123-456789abcdef";
    const fs::path abandoned = root / ("Glob2-export-" + std::to_string(child) + suffix);
    const fs::path active = root / ("Glob2-export-" + std::to_string(getpid()) + suffix);
    fs::create_directories(abandoned); fs::create_directories(active);
    file(abandoned / "export.game"); file(active / "export.game");
    fs::create_symlink(root / "outside" / dead, abandoned / "linked.game");
    REQUIRE(MobileTemporaryFiles::cleanupExports(root.string()) == 1);
    REQUIRE((!fs::exists(abandoned) && fs::exists(active / "export.game")));
    REQUIRE(fs::exists(root / "outside" / dead));
}
}
