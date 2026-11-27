// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Crash capture (docs/multiplayer/crash-reports.md#capture-in-the-game): what a
// crash leaves behind, and how the next launch turns it into a crash report.
//
// Each platform's capture writes the crash into the player's reports/crashes/
// directory: Crashpad's minidump on Android (moved out of its database at the
// next launch), MetricKit's crash diagnostic on iOS (delivered after the next
// launch), or an uncaught Java exception on Android. While the game runs, its
// recent log and the running match are mirrored into a small memory-mapped
// file, which survives the crash in the kernel's page cache, so the report also
// carries the log of the session that crashed.
//
// Nothing here runs on, or reads from, the simulation thread: the UI thread
// records the match's tick from the scene it draws.

#include "ProblemReport.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CrashCapture
{
// Whether this build captures crashes: Crashpad on Android, MetricKit on iOS.
// Builds without reports (problem_reports=0) never capture.
bool built();

// ---------------------------------------------------------------- session file

inline constexpr std::size_t SESSION_FILE_BYTES = 64 * 1024;

struct Match
{
	std::uint32_t tick = 0;
	std::uint32_t seed = 0;
	bool operator==(const Match &) const = default;
};

// What the previous run left in its session file.
struct Session
{
	std::string log;           // its last lines, oldest first
	std::int64_t startedAt = 0; // Unix seconds
	std::optional<Match> match;
};

// Opens reports/session.log for this run after keeping the previous run's as
// reports/previous-session.log. Returns false (and mirrors nothing) where the
// file cannot be mapped.
bool startSession(const std::filesystem::path &userDir);
// Unmaps the file (tests, and the end of the run).
void stopSession();
// Appends a log line to the session file; ProblemReport's log capture calls it.
void mirrorLine(std::string_view line);
// The match the UI is showing: its seed when it starts, then each drawn tick.
void matchStarted(std::uint32_t seed);
void matchTick(std::uint32_t tick);
void matchEnded();
// Reads a session file written by this module.
std::optional<Session> readSession(const std::filesystem::path &file);

// ---------------------------------------------------------------- crashes

enum class Kind
{
	Minidump,  // Crashpad: minidump.dmp
	MetricKit, // iOS: metrickit.json
	Java,      // Android: java-exception.txt
};
const char *kindName(Kind kind);        // the context's kind
const char *entryName(Kind kind);       // the bundle entry holding the crash
const char *fileSuffix(Kind kind);      // its file in reports/crashes/

struct Crash
{
	std::filesystem::path file;
	Kind kind = Kind::Minidump;
	std::int64_t capturedAt = 0; // Unix seconds (the file's time)
};

// At most this many crashes wait on disk, and at most CRASHES_BYTES in all;
// the oldest go first.
inline constexpr std::size_t CRASHES_COUNT = 5;
inline constexpr std::uintmax_t CRASHES_BYTES = 10 * 1024 * 1024;
std::filesystem::path crashesDirectory(const std::filesystem::path &userDir);
// Writes a crash a platform layer captured (MetricKit, Java) into
// reports/crashes/, keeping within the caps.
bool store(const std::filesystem::path &userDir, Kind kind, const std::string &bytes);
// The crashes waiting in reports/crashes/, newest first. The newest one without
// a log adopts the previous session's log (once), and older ones beyond the
// caps are deleted.
std::vector<Crash> pending(const std::filesystem::path &userDir);
// Removes a crash and its log.
void discard(const Crash &crash);
// A crash bundle for upload: context.json (kind, match), the crash, and the
// crashed session's log when there is one.
std::string bundle(const Crash &crash, const ProblemReport::Context &context);
// context.json for a crash: the problem report context with the crash's kind
// and the match that was running.
std::string contextJson(const ProblemReport::Context &context, Kind kind,
						const std::optional<Match> &match);
// The newest waiting crash's entries, for Report a Problem's bundle (Save to
// file keeps a crash that may not be sent).
std::vector<ProblemReport::Entry> newestCrashEntries(const std::filesystem::path &userDir);

// ---------------------------------------------------------------- consent

// The player's standing choice, Settings::alwaysSendCrashReports.
enum class Consent
{
	Ask,
	Always,
};
enum class Action
{
	Send,      // queue the reports without asking
	Ask,       // "Globulation 2 closed unexpectedly. Send a crash report?"
	KeepLocal, // may not send: keep them for Save to file
};
Action decide(ProblemReport::SendPolicy policy, Consent consent);

// Queues every waiting crash for upload in the problem report outbox, then
// removes it. Returns how many were queued.
std::size_t queueAll(const std::filesystem::path &userDir);
// Removes every waiting crash ("Don't send").
void discardAll(const std::filesystem::path &userDir);

// Set by a platform layer when it stored a crash after launch (MetricKit), so
// the main menu looks again; cleared by takeArrival().
void noteArrival();
bool takeArrival();

// ---------------------------------------------------------------- platform

// Starts the platform's capture for this run: moves Crashpad's minidumps from
// the last run into reports/crashes/ and starts its handler (Android), or
// subscribes to MetricKit (iOS). Call once, early, with the player's directory.
void startPlatform(const std::filesystem::path &userDir);
// Crashes the game on purpose (Settings > Privacy > Test crash report), from a
// game function so that the report shows a symbolicated stack.
[[noreturn]] void crashForTest();
} // namespace CrashCapture
