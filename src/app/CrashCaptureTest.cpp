// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Crash capture on the client (src/app/CrashCapture.h): the session file that
// keeps the log through a crash, the crashes waiting on disk and their caps,
// crash bundles and their context, and the consent decision.

#include "HeadlessGlobals.h"
#include "Glob2Test.h"
#include "CrashCapture.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iterator>

using namespace CrashCapture;
namespace fs = std::filesystem;

namespace
{
struct ScratchDir
{
	fs::path path;
	explicit ScratchDir(const std::string &name) : path(glob2test::profileDir() / ("crash-capture-" + name))
	{
		stopSession();
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~ScratchDir()
	{
		stopSession();
		std::error_code error;
		fs::remove_all(path, error);
	}
};

ProblemReport::Context sampleContext()
{
	ProblemReport::Context context;
	context.installId = "3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a";
	context.submissionId = "8e7d6c5b-4a39-4281-9706-f5e4d3c2b1a0";
	context.version = "0.9.6";
	context.versionMinor = 219;
	context.simRevision = 170;
	context.distribution = "google_play";
	context.platform = "android";
	context.os = "Android API 34";
	context.deviceClass = "phone";
	context.deviceTier = "mid";
	context.createdAt = "2026-11-26T12:34:56Z";
	return context;
}

std::string entry(const std::vector<ProblemReport::Entry> &entries, const std::string &name)
{
	for (const auto &e : entries)
		if (e.name == name)
			return e.bytes;
	return "<missing>";
}

nlohmann::json fixture(const std::string &name)
{
	std::ifstream file(glob2test::sourceRoot() / "services/online/packages/protocol/fixtures/valid/CrashReportContext" /
					   name);
	REQUIRE(file);
	const auto json = nlohmann::json::parse(file);
	return json.contains("value") ? json["value"] : json;
}

void sameShape(const nlohmann::json &ours, const nlohmann::json &theirs)
{
	REQUIRE(ours.size() == theirs.size());
	for (const auto &[key, expected] : theirs.items())
	{
		INFO(key);
		REQUIRE(ours.contains(key));
		CHECK(ours[key].type_name() == std::string(expected.type_name()));
	}
}
} // namespace

TEST_SUITE("CrashCapture")
{
	TEST_CASE("the session file keeps the newest whole lines and the running match")
	{
		ScratchDir dir("session");
		REQUIRE(startSession(dir.path));
		const fs::path file = dir.path / "reports" / "session.log";
		for (int i = 0; i < 4000; ++i)
			mirrorLine("line " + std::to_string(i) + " " + std::string(40, 'x'));
		matchStarted(123456789u);
		matchTick(4321);
		// Read while still mapped: what a crash would leave behind.
		auto session = readSession(file);
		REQUIRE(session);
		CHECK(session->log.size() <= SESSION_FILE_BYTES);
		const std::string last = "line 3999 " + std::string(40, 'x') + "\n";
		REQUIRE(session->log.size() > last.size());
		CHECK(session->log.substr(session->log.size() - last.size()) == last);
		CHECK(session->log.rfind("line ", 0) == 0); // starts on a whole line
		CHECK(session->log.find("line 0 ") == std::string::npos);
		REQUIRE(session->match);
		CHECK(*session->match == Match{4321, 123456789u});
		CHECK(session->startedAt > 0);
		matchEnded();
		CHECK_FALSE(readSession(file)->match);
		// A line longer than the whole ring is cut, so it is dropped like any
		// cut line (log lines are clamped to 512 bytes long before this).
		mirrorLine(std::string(SESSION_FILE_BYTES * 2, 'y') + "end");
		mirrorLine("after");
		session = readSession(file);
		REQUIRE(session);
		CHECK(session->log == "after\n");

		// The next run keeps this one as the previous session.
		stopSession();
		REQUIRE(startSession(dir.path));
		REQUIRE(readSession(dir.path / "reports" / "previous-session.log"));
		CHECK(readSession(file)->log.empty());
		CHECK_FALSE(readSession(dir.path / "missing.log"));
	}

	TEST_CASE("waiting crashes stay within five files and 10 MB, newest first")
	{
		ScratchDir dir("caps");
		for (int i = 0; i < 7; ++i)
			REQUIRE(store(dir.path, i % 2 ? Kind::MetricKit : Kind::Minidump, "crash " + std::to_string(i)));
		auto crashes = pending(dir.path);
		CHECK(crashes.size() == CRASHES_COUNT);
		CHECK_FALSE(store(dir.path, Kind::Minidump, ""));
		CHECK_FALSE(store(dir.path, Kind::Minidump, std::string(CRASHES_BYTES + 1, 'x')));
		// Two of 4 MB leave room for nothing else of that size.
		discardAll(dir.path);
		CHECK(pending(dir.path).empty());
		for (int i = 0; i < 3; ++i)
			REQUIRE(store(dir.path, Kind::Minidump, std::string(4 * 1024 * 1024, char('a' + i))));
		crashes = pending(dir.path);
		CHECK(crashes.size() == 2);
		for (const auto &crash : crashes)
			CHECK(fs::file_size(crash.file) == 4u * 1024 * 1024);
		CHECK(std::string(fileSuffix(Kind::Java)) == ".java.txt");
	}

	TEST_CASE("the newest crash takes the previous session's log once and bundles it")
	{
		ScratchDir dir("bundle");
		REQUIRE(startSession(dir.path));
		mirrorLine("Glob2 screen ready: game");
		matchStarted(77);
		matchTick(900);
		stopSession(); // the run that crashed
		REQUIRE(startSession(dir.path));
		REQUIRE(store(dir.path, Kind::Minidump, std::string("MDMP\x93\xa7", 6)));
		auto crashes = pending(dir.path);
		REQUIRE(crashes.size() == 1);
		CHECK_FALSE(fs::exists(dir.path / "reports" / "previous-session.log"));

		const auto entries = ProblemReport::unzip(bundle(crashes.front(), sampleContext()));
		REQUIRE(entries);
		CHECK((*entries)[0].name == "context.json");
		CHECK(entry(*entries, "minidump.dmp") == std::string("MDMP\x93\xa7", 6));
		CHECK(entry(*entries, "log.txt") == "Glob2 screen ready: game\n");
		const auto context = nlohmann::json::parse(entry(*entries, "context.json"));
		CHECK(context["kind"] == "crash");
		CHECK(context["description"] == "");
		CHECK(context["match"]["tick"] == 900);
		CHECK(context["match"]["seed"] == 77);

		// A later crash finds no previous log left to take.
		REQUIRE(store(dir.path, Kind::Java, "java.lang.IllegalStateException"));
		crashes = pending(dir.path);
		REQUIRE(crashes.size() == 2);
		const auto java = std::find_if(crashes.begin(), crashes.end(), [](const Crash &c) { return c.kind == Kind::Java; });
		REQUIRE(java != crashes.end());
		const auto javaEntries = ProblemReport::unzip(bundle(*java, sampleContext()));
		REQUIRE(javaEntries);
		CHECK(javaEntries->size() == 2);
		CHECK(entry(*javaEntries, "java-exception.txt") == "java.lang.IllegalStateException");
		CHECK_FALSE(nlohmann::json::parse(entry(*javaEntries, "context.json")).contains("match"));
	}

	TEST_CASE("crash contexts have exactly the fields of the server's fixtures")
	{
		const Match match{48210, 3735928559u};
		const auto crash = nlohmann::json::parse(contextJson(sampleContext(), Kind::Minidump, match));
		sameShape(crash, fixture("crash-in-match.json"));
		CHECK(crash["kind"] == "crash");
		const auto java = nlohmann::json::parse(contextJson(sampleContext(), Kind::Java, std::nullopt));
		sameShape(java, fixture("java-exception.json"));
		CHECK(java["kind"] == "java");
		CHECK(nlohmann::json::parse(contextJson(sampleContext(), Kind::MetricKit, std::nullopt))["kind"] == "metrickit");
	}

	TEST_CASE("only players who may send are asked, unless they always send")
	{
		using ProblemReport::SendPolicy;
		CHECK(decide(SendPolicy::Allowed, Consent::Ask) == Action::Ask);
		CHECK(decide(SendPolicy::Allowed, Consent::Always) == Action::Send);
		for (auto policy : {SendPolicy::AgeBand, SendPolicy::NotBuilt, SendPolicy::Portal})
		{
			CHECK(decide(policy, Consent::Ask) == Action::KeepLocal);
			CHECK(decide(policy, Consent::Always) == Action::KeepLocal);
		}
	}

	TEST_CASE("sending queues each crash in the outbox; Save to file can keep the newest")
	{
		ScratchDir dir("queue");
		REQUIRE(store(dir.path, Kind::MetricKit, "{\"callStackTree\":{}}"));
		ProblemReport::Choices choices;
		auto built = ProblemReport::build(choices, sampleContext(), dir.path);
		CHECK(std::find(built.attached.begin(), built.attached.end(), "metrickit.json") != built.attached.end());
		choices.crash = false;
		built = ProblemReport::build(choices, sampleContext(), dir.path);
		CHECK(std::find(built.attached.begin(), built.attached.end(), "metrickit.json") == built.attached.end());

		REQUIRE(store(dir.path, Kind::Minidump, "MDMP"));
		CHECK(queueAll(dir.path) == 2);
		CHECK(pending(dir.path).empty());
		ProblemReport::Outbox outbox(dir.path);
		const auto queued = outbox.pending();
		REQUIRE(queued.size() == 2);
		std::ifstream in(queued.back(), std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const auto entries = ProblemReport::unzip(bytes);
		REQUIRE(entries);
		const auto context = nlohmann::json::parse(entry(*entries, "context.json"));
		CHECK((context["kind"] == "crash" || context["kind"] == "metrickit"));
		CHECK(takeArrival() == false);
		noteArrival();
		CHECK(takeArrival());
		CHECK_FALSE(takeArrival());
	}
}
