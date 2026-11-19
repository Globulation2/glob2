// SPDX-License-Identifier: GPL-3.0-or-later
// Players must always know where a recording goes: the controls, the in-game
// message and the Recordings screen name the file or folder, and desktop offers
// to open that folder.
#include "HeadlessGlobals.h"
#include "RecordingFilesScreen.h"
#include "ui/RecordingNotice.h"
#include <ApplicationHost.h>
#include <GameplayRecording.h>
#include <ui/Presentation.h>
#include <ui/TextLayout.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace GAGGUI::ui;
namespace Recording = GAGCore::Recording;

namespace
{
bool contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}
Recording::Status statusOf(Recording::State state, const std::string &path)
{
	Recording::Status status;
	status.state = state;
	status.path = path;
	return status;
}
std::string allText(Host &host)
{
	std::string text;
	host.root()->visit([&](Node &node) { text += node.accessibleText() + "\n"; });
	return text;
}
} // namespace

TEST_SUITE("RecordingNotice")
{
	TEST_CASE("the recording controls say where a recording goes and where it was saved")
	{
		glob2test::HeadlessGlobals globals{{.loadStrings = true}};
		const std::string path = "/home/player/.glob2/videoshots/glob2-2026-11-18_14-05-02.mp4";
		const Recording::Completion none;
		CHECK(Glob2UI::recordingLocationLine(statusOf(Recording::State::Idle, ""), none).empty());
		const auto recording = Glob2UI::recordingLocationLine(statusOf(Recording::State::Recording, path), none);
		CHECK(contains(recording, "Recording to"));
		CHECK(contains(recording, path));
		CHECK(contains(Glob2UI::recordingLocationLine(statusOf(Recording::State::Finalizing, path), none), path));

		Recording::Completion saved;
		saved.serial = 1;
		saved.outputs = {path};
		const auto afterwards = Glob2UI::recordingLocationLine(statusOf(Recording::State::Complete, path), saved);
		CHECK(contains(afterwards, "saved to"));
		CHECK(contains(afterwards, path));
		// A new recording replaces the old destination with its own.
		const std::string next = "/home/player/.glob2/videoshots/glob2-2026-11-18_15-00-00.mp4";
		CHECK(contains(Glob2UI::recordingLocationLine(statusOf(Recording::State::Recording, next), saved), next));

		Recording::Completion failed;
		failed.serial = 2;
		failed.failed = true;
		failed.error = "Recording storage is full";
		CHECK(Glob2UI::recordingLocationLine(statusOf(Recording::State::Failed, path), failed).empty());
		CHECK(contains(Glob2UI::recordingFinishedMessage(failed), "Recording storage is full"));
		CHECK(contains(Glob2UI::recordingFinishedMessage(saved), path));
	}

	TEST_CASE("a recording ending is announced once and an earlier ending never")
	{
		std::optional<std::uint64_t> seen;
		Recording::Completion earlier;
		earlier.serial = 3;
		earlier.outputs = {"/videos/earlier.mp4"};
		CHECK_FALSE(Glob2UI::takeRecordingEnding(seen, earlier));
		CHECK_FALSE(Glob2UI::takeRecordingEnding(seen, earlier));
		Recording::Completion next = earlier;
		next.serial = 4;
		next.outputs = {"/videos/next.mp4"};
		const auto announced = Glob2UI::takeRecordingEnding(seen, next);
		REQUIRE(announced);
		CHECK(announced->outputs.front() == "/videos/next.mp4");
		CHECK_FALSE(Glob2UI::takeRecordingEnding(seen, next));
	}

	TEST_CASE("recording details give the date, duration and size in readable units")
	{
		Recording::RecordingFileEntry file;
		CHECK(Glob2UI::recordingFileDetails(file).empty());
		std::tm local{};
		local.tm_year = 2026 - 1900; local.tm_mon = 10; local.tm_mday = 18; local.tm_hour = 14; local.tm_min = 35;
		local.tm_isdst = -1;
		file.modified = std::int64_t(std::mktime(&local));
		file.durationUs = 23266666;
		file.bytes = 3025841;
		CHECK(Glob2UI::recordingFileDetails(file) == "2026-11-18 14:35 \xC2\xB7 0:23 \xC2\xB7 2.9 MB");
		file.durationUs = (3600LL + 2 * 60 + 5) * 1000000;
		file.bytes = 4096;
		file.modified = 0;
		CHECK(Glob2UI::recordingFileDetails(file) == "1:02:05 \xC2\xB7 4 KB");
	}

	TEST_CASE("folder links are file URLs with spaces and non-ASCII names percent-encoded")
	{
		CHECK(GAGCore::ApplicationHost::fileUrl("/home/ana/.glob2/videoshots") == "file:///home/ana/.glob2/videoshots");
		CHECK(GAGCore::ApplicationHost::fileUrl("/home/Zoë Smith/my videos") ==
			  "file:///home/Zo%C3%AB%20Smith/my%20videos");
		CHECK(GAGCore::ApplicationHost::fileUrl("C:\\Users\\Ana\\glob2 #1") == "file:///C:/Users/Ana/glob2%20%231");
	}

	TEST_CASE("the Recordings screen names its folder, offers to open it and describes each recording")
	{
		glob2test::HeadlessGlobals globals{{.loadStrings = true}};
		const auto folder = std::filesystem::u8path(Recording::recordingDirectory());
		std::filesystem::create_directories(folder);
		const auto video = folder / (std::string("glob2-2026-11-18_14-05-02") + Recording::Extension);
		std::ofstream(video, std::ios::binary) << std::string(2048, 'v');
		std::ofstream(video.string() + ".json") << "{\"version\":1,\"duration_us\":23266666}";
		struct Cleanup
		{
			std::filesystem::path video;
			~Cleanup()
			{
				std::filesystem::remove(video);
				std::filesystem::remove(video.string() + ".json");
			}
		} cleanup{video};

		RecordingFilesScreen screen;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (Recording::filesStatus().busy && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		REQUIRE_FALSE(Recording::filesStatus().busy);
		FixedTextMeasurer measurer;
		screen.host().setMeasurer(&measurer);
		screen.host().setPresentation(Presentation::forSurface(1024, 768));
		screen.host().update(1);
		const auto text = allText(screen.host());
		CHECK(contains(text, folder.string()));
		CHECK(contains(text, video.filename().string()));
		CHECK(contains(text, "0:23"));
		CHECK(contains(text, "2 KB"));
		auto *open = screen.host().find("recordings/open-folder");
		// Platforms that cannot open a folder (Android, iOS) omit the button.
		if (GAGCore::ApplicationHost::canOpenFolder())
		{
			REQUIRE(open);
			CHECK(contains(open->accessibleText(), "Open folder"));
		}
		else
			CHECK_FALSE(open);
		CHECK(screen.host().find("export/" + video.string()));
	}
}
