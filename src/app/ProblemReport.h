// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Report a Problem (docs/multiplayer/problem-reports.md): a player-made bundle of
// the recent log, the newest save and replay, and a short context, that the game
// uploads to the official glob2-online instance or writes to a file.
//
// The bundle is a zip of stored entries: context.json (the CrashReportContext
// schema in services/online/packages/protocol/src/crashReports.ts), and as the
// player chooses log.txt, save.game and last.replay (with .gz appended when
// the file on disk is gzip-compressed). "Save to file" writes the
// same bytes, so a bundle saved offline reads like an uploaded one.
//
// Nothing here runs on, or reads from, the simulation thread: collecting copies
// files from the player's directory on the UI thread, and uploads are polled
// like every other HttpFetch.

#include "AgeGate.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ProblemReport
{
// ---------------------------------------------------------------- log tail

// Keeps the last lines written to std::cout, std::cerr, std::clog and SDL's
// log (the "Glob2 screen ready" breadcrumbs among them) while passing every
// byte on unchanged. install() is called once, early in main(); without it
// recentLog() returns only what tests appended.
inline constexpr std::size_t LOG_LINES = 200;
inline constexpr std::size_t LOG_LINE_BYTES = 512;
void installLogCapture();
// Restores the streams, SDL's log output and the screen observer (tests).
void uninstallLogCapture();
void appendLogLine(const std::string &line);
std::string recentLog();
void clearLog(); // tests

// ---------------------------------------------------------------- who may send

// Whether this build can send reports at all (feature_problem_reports, off for
// F-Droid, distribution packages and the China and Fire tablet editions).
bool sendingBuilt();

enum class SendPolicy
{
	Allowed,
	NotBuilt,   // compiled out: Save to file only
	AgeBand,    // under 13 or no declared age: Save to file only
	Portal,     // a web portal that forbids signing in to our servers
};
SendPolicy sendPolicy(bool built, AgeGate::Band band, bool portalAllowsOnline);
// The policy of this run: sendingBuilt(), AgeGate::local() and PortalPolicy.
SendPolicy currentSendPolicy();

// ---------------------------------------------------------------- bundles

struct Choices
{
	std::string description; // "What happened?", at most DESCRIPTION_LIMIT bytes
	bool log = true;
	bool save = true;
	bool replay = true;
	// The newest crash waiting on the device (src/app/CrashCapture.h), if any.
	bool crash = true;
};
inline constexpr std::size_t DESCRIPTION_LIMIT = 2000;
// The server accepts 10 MB; attachments that would pass this are left out.
inline constexpr std::size_t BUNDLE_LIMIT = 10 * 1024 * 1024 - 64 * 1024;

// The fields every report carries, besides the player's text.
struct Context
{
	std::string installId;
	std::string submissionId; // random per report: the server stores a retry once
	std::string version;
	int versionMinor = 0;
	int simRevision = 0;
	std::string distribution;
	std::string platform;
	std::string os;
	std::string deviceClass;
	std::string deviceTier;
	std::string createdAt; // RFC 3339, UTC
};
// This build and device, with the install ID from `userDir`.
Context currentContext(const std::filesystem::path &userDir);
// context.json for a bundle.
std::string contextJson(const Context &context, const std::string &description);

struct Entry
{
	std::string name;
	std::string bytes;
};
// A zip archive of stored entries (no compression; saves and replays are already
// compressed). Entry names must be short plain file names.
std::string zip(const std::vector<Entry> &entries);
// Parses an archive written by zip(); for tests and the outbox's sanity check.
std::optional<std::vector<Entry>> unzip(const std::string &archive);

// What a bundle built from these choices would attach, by name (context.json
// first), and which wanted files were left out for size.
struct Bundle
{
	std::string bytes;
	std::vector<std::string> attached;
	std::vector<std::string> tooLarge;
};
// Builds a bundle from the player's directory: the newest games/*.game and
// replays/*.replay there (never the shipped content).
Bundle build(const Choices &choices, const Context &context, const std::filesystem::path &userDir);
// The newest regular file named *.<extension> or *.<extension>.gz directly in
// `dir`, if any.
std::optional<std::filesystem::path> newestFile(const std::filesystem::path &dir,
											   const std::string &extension);
// A suggested name for Save to file: glob2-report-YYYYMMDD-HHMMSS.zip.
std::string fileName(const Context &context);

// The player's directory (FileManager's first, writable root).
std::filesystem::path userDirectory();
// A path as UTF-8 (FileManager and ApplicationHost take UTF-8 everywhere).
std::string utf8(const std::filesystem::path &path);
// At most `limit` bytes of `text`, never ending inside a UTF-8 sequence.
std::string clampUtf8(const std::string &text, std::size_t limit);

// ---------------------------------------------------------------- outbox

// Reports that could not be sent wait in reports/outbox/ and go out later.
// At most OUTBOX_COUNT reports and OUTBOX_BYTES on disk; the oldest go first.
inline constexpr std::size_t OUTBOX_COUNT = 5;
inline constexpr std::uintmax_t OUTBOX_BYTES = 10 * 1024 * 1024;
class Outbox
{
  public:
	explicit Outbox(std::filesystem::path userDir);
	// Stores a bundle, dropping the oldest to stay within the caps. False when
	// it cannot be written (or is itself larger than OUTBOX_BYTES).
	bool add(const std::string &bundle);
	// Pending files, oldest first.
	std::vector<std::filesystem::path> pending() const;
	void remove(const std::filesystem::path &file);

  private:
	std::filesystem::path dir;
};

// ---------------------------------------------------------------- upload

// The upload endpoint on the official instance, or on the origin in
// GLOB2_PROBLEM_REPORT_ORIGIN (development and device checks).
std::string endpoint();

enum class UploadState
{
	Pending,
	Sent,      // id() is the report ID to show
	Refused,   // the server refused this bundle (4xx other than 429): never retry
	Retry,     // no answer, 429 or 5xx: keep it and try again later
};
class Upload
{
  public:
	virtual ~Upload() = default;
	virtual UploadState poll() = 0;
	virtual std::string id() const = 0;
	virtual std::string error() const = 0;
};
// Starts sending a bundle. Tests replace the transport with setTransport.
std::unique_ptr<Upload> send(const std::string &bundle);
// What an HTTP answer means for a report; exposed for tests.
UploadState classify(int status);
std::optional<std::string> receiptId(const std::string &body);
using Transport = std::function<std::unique_ptr<Upload>(const std::string &bundle)>;
void setTransport(Transport transport); // empty restores HttpFetch

// Sends queued reports one at a time, never more often than every few minutes
// after a failure. Call it from a menu's timer; it does nothing while reports
// may not be sent (currentSendPolicy()) and costs nothing with an empty outbox.
void pumpOutbox(std::uint32_t tickMs);
// Forgets the pump's state (tests).
void resetOutboxPump();
} // namespace ProblemReport
