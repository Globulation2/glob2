// SPDX-License-Identifier: GPL-3.0-or-later
#include "doctest.h"
#include <GameplayRecording.h>
#include <filesystem>

TEST_SUITE("RecordingDisabled")
{
    TEST_CASE("unavailable recorder never starts capture or touches files")
    {
        using namespace GAGCore::Recording;
        CHECK_FALSE(supported());
        CHECK_FALSE(available());
        CHECK_FALSE(toggle());
        Recorder local;
        const auto path = std::filesystem::temp_directory_path() / "glob2-disabled-recording.mp4";
        const bool existed = std::filesystem::exists(path);
        CHECK_FALSE(local.start(path.string()));
        CHECK_FALSE(local.active());
        CHECK_FALSE(local.wantsFrame());
        CHECK_FALSE(local.reserveCapture().has_value());
        local.audio(nullptr, 0);
        local.beginMatch("local", "map", 0, 1);
        local.matchFrame(2, false, 1);
        local.stop();
        local.shutdown();
        CHECK(local.status().state == State::Idle);
        requestFiles();
        CHECK_FALSE(takeFilesRequest());
        refreshFiles();
        recoverFile(path.string());
        deleteFile(path.string());
        CHECK(filesStatus().files.empty());
        CHECK(std::filesystem::exists(path) == existed);
    }
}
