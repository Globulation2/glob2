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
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CrashCapture
{
// Whether this build captures crashes: Crashpad on Android and Linux, MetricKit
// on iOS and macOS, dbghelp minidumps on Windows, error hooks in the browser.
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
	Minidump,  // Crashpad (Android, Linux) or dbghelp (Windows): minidump.dmp
	MetricKit, // iOS and macOS: metrickit.json
	Java,      // Android: java-exception.txt
	Web,       // the browser: web-error.json
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
// Where Always send is read (the game's settings); unset reads false.
void setAlwaysSendSource(std::function<bool()> source);
bool alwaysSends();

// Queues every waiting crash for upload in the problem report outbox, then
// removes it. Returns how many were queued.
std::size_t queueAll(const std::filesystem::path &userDir);
// Removes every waiting crash ("Don't send").
void discardAll(const std::filesystem::path &userDir);

// Set by a platform layer when it stored a crash after launch (MetricKit), so
// the main menu looks again; cleared by takeArrival().
void noteArrival();
bool takeArrival();

// ---------------------------------------------------------------- non-fatal errors

// Failures that do not crash the game: a map or saved game that fails to load
// (GameLoadScreen), and an online match that fails (OnlineMatch::fail). They go
// to glob2-online as kind "error" (error.json) only for players who chose Always
// send, at most ERRORS_PER_DAY a day per install and once per category and
// message per run.
enum class ErrorCategory
{
	MapLoad,
	Network,
};
const char *categoryName(ErrorCategory category);
inline constexpr int ERRORS_PER_DAY = 5;
inline constexpr std::size_t ERROR_MESSAGE_BYTES = 1024;
// Queues a non-fatal error report when the player allows it; returns whether
// one was queued.
bool reportError(ErrorCategory category, const std::string &message);
// The error.json entry (protocol ErrorReport) and its bundle; for tests.
std::string errorJson(ErrorCategory category, const std::string &message);
// Whether the daily cap in reports/errors-today allows another, counting it.
bool takeErrorSlot(const std::filesystem::path &userDir, const std::string &utcDay);
void resetErrorsForTests();

// ---------------------------------------------------------------- session counts

// Crash-free rates (docs/multiplayer/crash-reports.md#release-gates) count the
// launches of installs that chose Always send, per UTC day, in
// reports/sessions.json. POST /api/v1/crash-sessions takes a day's running
// total, so a resend is harmless: today's at most hourly, a finished day's once.
// Turning Always send off deletes the counts.
struct SessionDay
{
	int sessions = 0; // launches counted that day
	int sent = 0;     // the total the server last accepted
	std::int64_t sentAt = 0;
};
using SessionDays = std::map<std::string, SessionDay>; // "YYYY-MM-DD"
inline constexpr int SESSION_DAYS_KEPT = 7;
inline constexpr int SESSIONS_PER_DAY_MAX = 500;
// Counts this launch (and forgets days older than a week).
void countSession(const std::filesystem::path &userDir, const std::string &today);
SessionDays readSessions(const std::filesystem::path &userDir);
void writeSessions(const std::filesystem::path &userDir, const SessionDays &days);
// The day to send next, if any: a finished day not yet accepted in full, or
// today once an hour after its last accepted total.
std::optional<std::string> nextSessionPing(const SessionDays &days, const std::string &today, std::int64_t now);
// The request body for a day (protocol CrashSessionPing).
std::string sessionPingJson(const ProblemReport::Context &context, const std::string &day, int sessions);
// Applies an answer: accepted, refused (drop the day) or retry later.
void applySessionAnswer(SessionDays &days, const std::string &day, int sessions, int status,
						const std::string &today, std::int64_t now);
// Sends waiting counts from the main menu's timer; does nothing unless the
// player may send and chose Always send.
void pumpSessions(std::uint32_t tickMs);
void resetSessionPumpForTests();

// ---------------------------------------------------------------- platform

// Starts the platform's capture for this run: moves Crashpad's minidumps from
// the last run into reports/crashes/ and starts its handler (Android), or
// subscribes to MetricKit (iOS). Call once, early, with the player's directory.
void startPlatform(const std::filesystem::path &userDir);
// Crashes the game on purpose (Settings > Privacy > Test crash report), from a
// game function so that the report shows a symbolicated stack.
[[noreturn]] void crashForTest();
} // namespace CrashCapture
