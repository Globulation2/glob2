// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Report a Problem on the client (src/app/ProblemReport.h): the bundle format,
// the context's contract with the server, who may send, the log tail, the
// outbox caps and the outbox pump.

#include "HeadlessGlobals.h"
#include "Glob2Test.h"
#include "ProblemReport.h"
#include <ApplicationHost.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace ProblemReport;
namespace fs = std::filesystem;

namespace
{
// An empty scratch directory standing in for the player's directory.
struct ScratchDir
{
	fs::path path;
	explicit ScratchDir(const std::string &name) : path(glob2test::profileDir() / ("problem-report-" + name))
	{
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~ScratchDir()
	{
		std::error_code error;
		fs::remove_all(path, error);
	}
};

void writeFile(const fs::path &path, const std::string &bytes)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << bytes;
}

Context sampleContext()
{
	Context context;
	context.installId = "3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a";
	context.submissionId = "8e7d6c5b-4a39-4281-9706-f5e4d3c2b1a0";
	context.version = "0.9.6";
	context.versionMinor = 219;
	context.simRevision = 170;
	context.distribution = "direct";
	context.platform = "linux";
	context.os = "Linux";
	context.deviceClass = "desktop";
	context.deviceTier = "high";
	context.createdAt = "2026-11-26T12:34:56Z";
	return context;
}

std::string entry(const std::vector<Entry> &entries, const std::string &name)
{
	for (const auto &e : entries)
		if (e.name == name)
			return e.bytes;
	return "<missing>";
}

class ScriptedUpload : public Upload
{
  public:
	ScriptedUpload(UploadState result, int &started) : result(result) { ++started; }
	UploadState poll() override { return result; }
	std::string id() const override { return "K7M2QX9D"; }
	std::string error() const override { return "scripted"; }

  private:
	UploadState result;
};

struct ScopedAge
{
	AgeGate::Store previous = AgeGate::installedStore();
	explicit ScopedAge(AgeGate::Band band)
	{
		AgeGate::setStore({[band] { return AgeGate::Local{band, AgeGate::Source::Self}; },
						   [](const AgeGate::Local &) { return true; }});
	}
	~ScopedAge()
	{
		AgeGate::setStore(previous);
		setTransport({});
		resetOutboxPump();
	}
};
} // namespace

TEST_SUITE("ProblemReport")
{
	TEST_CASE("bundles are zips of stored entries that round-trip")
	{
		const std::vector<Entry> entries{{"context.json", "{}"},
										 {"log.txt", std::string("line\n") + std::string(70000, 'x')},
										 {"empty.txt", ""}};
		const std::string archive = zip(entries);
		REQUIRE(archive.substr(0, 4) == std::string("PK\x03\x04", 4));
		const auto back = unzip(archive);
		REQUIRE(back);
		REQUIRE(back->size() == 3);
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			CHECK((*back)[i].name == entries[i].name);
			CHECK((*back)[i].bytes == entries[i].bytes);
		}
		std::string corrupt = archive;
		corrupt[43] ^= 1; // inside context.json's data: the CRC no longer matches
		CHECK_FALSE(unzip(corrupt));
		CHECK_FALSE(unzip("PK"));
	}

	TEST_CASE("the context has exactly the fields of the server's CrashReportContext fixture")
	{
		std::ifstream file(glob2test::sourceRoot() /
						   "services/online/packages/protocol/fixtures/valid/CrashReportContext/problem-report.json");
		REQUIRE(file);
		const auto fixture = nlohmann::json::parse(file);
		const auto value = fixture.contains("value") ? fixture["value"] : fixture;
		const auto ours = nlohmann::json::parse(contextJson(sampleContext(), "It froze."));
		REQUIRE(ours.size() == value.size());
		for (const auto &[key, expected] : value.items())
		{
			INFO(key);
			REQUIRE(ours.contains(key));
			CHECK(ours[key].type_name() == std::string(expected.type_name()));
		}
		CHECK(ours["description"] == "It froze.");
		CHECK(ours["kind"] == "problem");
		CHECK(fileName(sampleContext()) == "glob2-report-20261126-123456.zip");
	}

	TEST_CASE("the live context describes this build and keeps one install ID")
	{
		ScratchDir dir("context");
		const Context first = currentContext(dir.path);
		const Context second = currentContext(dir.path);
		CHECK(first.installId.size() == 36);
		CHECK(first.installId[14] == '4');
		CHECK(first.installId == second.installId);
		CHECK(first.submissionId.size() == 36);
		CHECK(first.submissionId != second.submissionId); // one per report
		CHECK_FALSE(first.version.empty());
		CHECK(first.simRevision > 0);
		CHECK((first.deviceTier == "low" || first.deviceTier == "mid" || first.deviceTier == "high"));
		CHECK((first.deviceClass == "desktop" || first.deviceClass == "tablet" || first.deviceClass == "phone"));
		CHECK(first.createdAt.size() == 20);
		// A damaged ID file is replaced, not sent.
		writeFile(dir.path / "reports/install-id", "not-a-uuid\n");
		CHECK(currentContext(dir.path).installId != "not-a-uuid");
	}

	TEST_CASE("only players 13 or over, in builds and pages that allow it, may send")
	{
		using AgeGate::Band;
		CHECK(sendPolicy(true, Band::ThirteenPlus, true) == SendPolicy::Allowed);
		CHECK(sendPolicy(true, Band::Under13, true) == SendPolicy::AgeBand);
		CHECK(sendPolicy(true, Band::Unknown, true) == SendPolicy::AgeBand);
		CHECK(sendPolicy(false, Band::ThirteenPlus, true) == SendPolicy::NotBuilt);
		CHECK(sendPolicy(true, Band::ThirteenPlus, false) == SendPolicy::Portal);
	}

	TEST_CASE("the log tail keeps the newest bounded lines and sees the standard streams")
	{
		clearLog();
		for (std::size_t i = 0; i < LOG_LINES + 20; ++i)
			appendLogLine("line " + std::to_string(i));
		const std::string log = recentLog();
		CHECK(log.find("line 19\n") == std::string::npos);
		CHECK(log.find("line 20\n") == 0);
		CHECK(log.find("line " + std::to_string(LOG_LINES + 19) + "\n") != std::string::npos);
		clearLog();
		appendLogLine(std::string(LOG_LINE_BYTES - 1, 'a') + "\xC3\xA9tail");
		CHECK(recentLog().size() == LOG_LINE_BYTES - 1 + 1); // the split é is dropped whole
		// Process-wide streams and SDL's log are restored before the case ends.
		struct Restore
		{
			~Restore() { uninstallLogCapture(); }
		} restore;
		installLogCapture();
		clearLog();
		std::clog << "Glob2 screen ready: " << "MainMenuScreen" << std::endl;
		std::cerr << "partial ";
		std::cerr << "line\n";
		CHECK(recentLog() == "Glob2 screen ready: MainMenuScreen\npartial line\n");
		clearLog();
		// Screen changes are breadcrumbs on every host, printed or not.
		GAGCore::ApplicationHost::screenChanged("17ProblemReportScreen");
		CHECK(recentLog().find("Glob2 screen ready: 17ProblemReportScreen\n") != std::string::npos);
		uninstallLogCapture();
		clearLog();
		std::clog << "after uninstall" << std::endl;
		GAGCore::ApplicationHost::screenChanged("17ProblemReportScreen");
		CHECK(recentLog().empty());
	}

	TEST_CASE("bundles attach the newest save and replay from the player's directory as chosen")
	{
		ScratchDir dir("bundle");
		writeFile(dir.path / "games/old.game", "old save");
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		writeFile(dir.path / "games/new.game", "new save");
		writeFile(dir.path / "games/notes.txt", "not a save");
		writeFile(dir.path / "replays/match.replay.gz", "replay bytes");
		clearLog();
		appendLogLine("Glob2 screen ready: MainMenuScreen");

		Choices choices;
		choices.description = "Units froze.";
		auto bundle = build(choices, sampleContext(), dir.path);
		CHECK(bundle.attached == std::vector<std::string>{"context.json", "log.txt", "save.game", "last.replay.gz"});
		auto entries = unzip(bundle.bytes);
		REQUIRE(entries);
		CHECK(entry(*entries, "save.game") == "new save");
		CHECK(entry(*entries, "last.replay.gz") == "replay bytes");
		CHECK(entry(*entries, "log.txt") == "Glob2 screen ready: MainMenuScreen\n");
		CHECK(nlohmann::json::parse(entry(*entries, "context.json"))["description"] == "Units froze.");

		choices = {};
		choices.log = choices.save = choices.replay = false;
		choices.description = std::string(DESCRIPTION_LIMIT - 1, 'a') + "\xC3\xA9";
		bundle = build(choices, sampleContext(), dir.path);
		CHECK(bundle.attached == std::vector<std::string>{"context.json"});
		entries = unzip(bundle.bytes);
		REQUIRE(entries);
		CHECK(nlohmann::json::parse(entry(*entries, "context.json"))["description"].get<std::string>().size() ==
			  DESCRIPTION_LIMIT - 1);

		writeFile(dir.path / "replays/huge.replay", std::string(BUNDLE_LIMIT + 1, 'r'));
		bundle = build(Choices{}, sampleContext(), dir.path);
		CHECK(bundle.tooLarge == std::vector<std::string>{"last.replay"});
		CHECK(bundle.bytes.size() < BUNDLE_LIMIT);
		clearLog();
	}

	TEST_CASE("UTF-8 text is clamped without splitting a character")
	{
		CHECK(clampUtf8("abc", 5) == "abc");
		CHECK(clampUtf8("ab\xC3\xA9", 3) == "ab");
		CHECK(clampUtf8("ab\xC3\xA9", 4) == "ab\xC3\xA9");
		CHECK(clampUtf8("\xE2\x82\xAC\xE2\x82\xAC", 5) == "\xE2\x82\xAC");
	}

	TEST_CASE("reports queued in the same millisecond keep separate files")
	{
		ScratchDir dir("outbox-names");
		for (int i = 0; i < 3; ++i)
			REQUIRE(Outbox(dir.path).add("report " + std::to_string(i)));
		CHECK(Outbox(dir.path).pending().size() == 3);
	}

	TEST_CASE("the outbox keeps at most five reports and 10 MB, dropping the oldest")
	{
		ScratchDir dir("outbox");
		Outbox outbox(dir.path);
		for (int i = 0; i < 7; ++i)
			REQUIRE(outbox.add("report " + std::to_string(i)));
		auto files = outbox.pending();
		REQUIRE(files.size() == OUTBOX_COUNT);
		std::ifstream oldest(files.front(), std::ios::binary);
		CHECK(std::string(std::istreambuf_iterator<char>(oldest), {}) == "report 2");

		ScratchDir big("outbox-bytes");
		Outbox bytes(big.path);
		const std::string four(4 * 1024 * 1024, 'b');
		for (int i = 0; i < 3; ++i)
			REQUIRE(bytes.add(four));
		CHECK(bytes.pending().size() == 2);
		CHECK_FALSE(bytes.add(std::string(OUTBOX_BYTES + 1, 'x')));
	}

	TEST_CASE("answers decide whether a report is kept for later")
	{
		CHECK(classify(201) == UploadState::Sent);
		CHECK(classify(429) == UploadState::Retry);
		CHECK(classify(503) == UploadState::Retry);
		CHECK(classify(400) == UploadState::Refused);
		CHECK(classify(413) == UploadState::Refused);
		CHECK(receiptId(R"({"id":"K7M2QX9D"})") == std::optional<std::string>("K7M2QX9D"));
		CHECK_FALSE(receiptId(R"({"id":"k7m2qx9d"})"));
		CHECK_FALSE(receiptId("not json"));
		CHECK(endpoint().find("/api/v1/crash-reports") != std::string::npos);
	}

	TEST_CASE("the main menu sends queued reports one at a time and keeps failures")
	{
		glob2test::HeadlessGlobals globals;
		const fs::path user = userDirectory();
		REQUIRE_FALSE(user.empty());
		std::error_code error;
		fs::remove_all(user / "reports/outbox", error);
		Outbox outbox(user);
		REQUIRE(outbox.add(zip({{"context.json", "{}"}})));
		REQUIRE(outbox.add(zip({{"context.json", "{}"}})));
		int started = 0;
		UploadState next = UploadState::Retry;
		{
			ScopedAge child(AgeGate::Band::Under13);
			setTransport([&](const std::string &) { return std::make_unique<ScriptedUpload>(next, started); });
			for (std::uint32_t tick = 0; tick < 600000; tick += 1000)
				pumpOutbox(tick);
			CHECK(started == 0); // never for players under 13
		}
		ScopedAge adult(AgeGate::Band::ThirteenPlus);
		setTransport([&](const std::string &) { return std::make_unique<ScriptedUpload>(next, started); });
		pumpOutbox(0);
		CHECK(started == 0); // waits a little after the menu opens
		pumpOutbox(20000);
		pumpOutbox(21000);
		CHECK(started == 1);
		CHECK(outbox.pending().size() == 2); // a failure is kept
		pumpOutbox(60000);
		CHECK(started == 1); // and retried only minutes later
		next = UploadState::Sent;
		pumpOutbox(400000);
		pumpOutbox(401000); // sent: removed, and the next one starts at once
		pumpOutbox(402000);
		pumpOutbox(403000);
		CHECK(started == 3);
		CHECK(outbox.pending().empty());
		fs::remove_all(user / "reports", error);
	}
}
