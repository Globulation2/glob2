// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProblemReport.h"
#include "CrashCapture.h"
#include "DeviceProfile.h"
#include "HttpFetch.h"
#include "InstanceConfig.h"
#include "PortalPolicy.h"
#include "SimRevision.h"
#include "Version.h"
#include <ApplicationHost.h>
#include <FileManager.h>
#include <InterfacePresentation.h>
#include <Toolkit.h>
#include <SDL3/SDL.h>
#include <glob2/BuildConfig.h>
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <streambuf>
#include <system_error>

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif
#ifndef GLOB2_PROBLEM_REPORTS
#define GLOB2_PROBLEM_REPORTS 1
#endif
#ifndef GLOB2_DISTRIBUTION_NAME
#define GLOB2_DISTRIBUTION_NAME "direct"
#endif

namespace fs = std::filesystem;

namespace ProblemReport
{
std::string clampUtf8(const std::string &text, std::size_t limit)
{
	if (text.size() <= limit)
		return text;
	// Cut before the sequence that would cross the limit.
	std::size_t cut = limit;
	while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
		--cut;
	return text.substr(0, cut);
}

std::string utf8(const fs::path &path)
{
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

// ---------------------------------------------------------------- log tail

namespace
{
std::mutex &logMutex()
{
	static std::mutex mutex;
	return mutex;
}
std::deque<std::string> &logLines()
{
	static std::deque<std::string> lines;
	return lines;
}

// Caller holds logMutex().
void pushLine(std::string line)
{
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	line = clampUtf8(line, LOG_LINE_BYTES);
	auto &lines = logLines();
	// The session file keeps the log through a crash (src/app/CrashCapture.h).
	CrashCapture::mirrorLine(line);
	lines.push_back(std::move(line));
	while (lines.size() > LOG_LINES)
		lines.pop_front();
}

// Passes everything to the original buffer and keeps complete lines.
class TeeBuffer : public std::streambuf
{
  public:
	explicit TeeBuffer(std::streambuf *target) : target(target) {}

  protected:
	int overflow(int c) override
	{
		if (c == traits_type::eof())
			return target->pubsync() == 0 ? traits_type::not_eof(c) : traits_type::eof();
		const char ch = static_cast<char>(c);
		record(&ch, 1);
		return target->sputc(ch);
	}
	std::streamsize xsputn(const char *s, std::streamsize n) override
	{
		record(s, n);
		return target->sputn(s, n);
	}
	int sync() override { return target->pubsync(); }

  private:
	void record(const char *s, std::streamsize n)
	{
		std::lock_guard<std::mutex> lock(logMutex());
		for (std::streamsize i = 0; i < n; ++i)
		{
			if (s[i] == '\n')
			{
				pushLine(std::move(partial));
				partial.clear();
			}
			else if (partial.size() <= LOG_LINE_BYTES)
				partial.push_back(s[i]);
		}
	}
	std::streambuf *target;
	std::string partial;
};

SDL_LogOutputFunction previousOutput = nullptr;
void *previousUserData = nullptr;

void SDLCALL sdlOutput(void *, int category, SDL_LogPriority priority, const char *message)
{
	if (previousOutput)
		previousOutput(previousUserData, category, priority, message);
	if (message)
		appendLogLine(message);
}
} // namespace

namespace
{
struct Installed
{
	std::ostream *stream;
	std::streambuf *original;
	TeeBuffer *tee;
};
std::vector<Installed> &installedTees()
{
	static std::vector<Installed> tees;
	return tees;
}
} // namespace

void installLogCapture()
{
	auto &tees = installedTees();
	if (!tees.empty())
		return;
	// Kept until uninstallLogCapture(): the standard streams flush through them
	// during exit.
	for (std::ostream *stream : {&std::cout, &std::cerr, &std::clog})
	{
		auto *tee = new TeeBuffer(stream->rdbuf());
		tees.push_back({stream, stream->rdbuf(tee), tee});
	}
	SDL_GetLogOutputFunction(&previousOutput, &previousUserData);
	SDL_SetLogOutputFunction(sdlOutput, nullptr);
	// Mobile hosts already SDL_Log each screen; the others tell the observer,
	// which keeps the same breadcrumb without printing it.
	GAGCore::ApplicationHost::setScreenObserver(
		[](const char *name) { appendLogLine(std::string("Glob2 screen ready: ") + (name ? name : "")); });
}

void uninstallLogCapture()
{
	auto &tees = installedTees();
	if (tees.empty())
		return;
	for (auto &installed : tees)
	{
		installed.stream->flush();
		installed.stream->rdbuf(installed.original);
		delete installed.tee;
	}
	tees.clear();
	SDL_SetLogOutputFunction(previousOutput, previousUserData);
	previousOutput = nullptr;
	previousUserData = nullptr;
	GAGCore::ApplicationHost::setScreenObserver({});
}

void appendLogLine(const std::string &line)
{
	std::lock_guard<std::mutex> lock(logMutex());
	std::size_t start = 0;
	while (true)
	{
		const auto end = line.find('\n', start);
		pushLine(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
}

std::string recentLog()
{
	std::lock_guard<std::mutex> lock(logMutex());
	std::string text;
	for (const auto &line : logLines())
		text += line + '\n';
	return text;
}

void clearLog()
{
	std::lock_guard<std::mutex> lock(logMutex());
	logLines().clear();
}

// ---------------------------------------------------------------- who may send

bool sendingBuilt()
{
	return GLOB2_PROBLEM_REPORTS != 0;
}

SendPolicy sendPolicy(bool built, AgeGate::Band band, bool portalAllowsOnline)
{
	if (!built)
		return SendPolicy::NotBuilt;
	if (!portalAllowsOnline)
		return SendPolicy::Portal;
	// Players under 13, or who never declared an age, never send: like chat,
	// reporting needs ThirteenPlus (docs/multiplayer/identity.md#age-bands).
	if (!AgeGate::socialFeatures(band))
		return SendPolicy::AgeBand;
	return SendPolicy::Allowed;
}

SendPolicy currentSendPolicy()
{
	return sendPolicy(sendingBuilt(), AgeGate::local().band, PortalPolicy::onlineSignIn());
}

// ---------------------------------------------------------------- context

namespace
{
const char *platformName()
{
#if defined(__EMSCRIPTEN__)
	return "web";
#elif defined(__ANDROID__)
	return "android";
#elif defined(__APPLE__)
	return std::string_view(SDL_GetPlatform()) == "macOS" ? "macos" : "ios";
#elif defined(_WIN32)
	return "windows";
#elif defined(__linux__)
	return "linux";
#else
	return "other";
#endif
}

std::string operatingSystem()
{
	std::string os = SDL_GetPlatform();
#ifdef __ANDROID__
	os += " API " + std::to_string(SDL_GetAndroidSDKVersion());
#endif
	return os.substr(0, 128);
}

std::string utcStamp(const char *format)
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	char text[32];
	std::strftime(text, sizeof text, format, &utc);
	return text;
}

std::string newUuid()
{
	// Version 4 UUID from the system entropy source; nothing to do with the
	// simulation's random numbers.
	std::random_device entropy;
	std::uniform_int_distribution<int> byte(0, 255);
	unsigned char bytes[16];
	for (auto &b : bytes)
		b = static_cast<unsigned char>(byte(entropy));
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
	static const char *hex = "0123456789abcdef";
	std::string id;
	for (int i = 0; i < 16; ++i)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
			id += '-';
		id += hex[bytes[i] >> 4];
		id += hex[bytes[i] & 15];
	}
	return id;
}

bool validInstallId(const std::string &id)
{
	static const std::regex uuid("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
	return std::regex_match(id, uuid);
}

std::optional<std::string> readFile(const fs::path &path, std::uintmax_t limit)
{
	std::error_code error;
	const auto size = fs::file_size(path, error);
	if (error || size > limit)
		return std::nullopt;
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return std::nullopt;
	std::string bytes(static_cast<std::size_t>(size), '\0');
	if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
		return std::nullopt;
	return bytes;
}

bool writeFileAtomic(const fs::path &path, const std::string &bytes)
{
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	const fs::path temporary = path.string() + ".part";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
			return false;
	}
	fs::rename(temporary, path, error);
	if (error)
	{
		fs::remove(temporary, error);
		return false;
	}
	return true;
}

void persist()
{
	// Browser builds keep the user directory in IndexedDB; flush it soon. The
	// handle may be released while pending.
	(void)GAGCore::ApplicationHost::persistStorage();
}

std::string installId(const fs::path &userDir)
{
	const fs::path file = userDir / "reports" / "install-id";
	if (auto stored = readFile(file, 64))
	{
		std::string id = *stored;
		while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
			id.pop_back();
		if (validInstallId(id))
			return id;
	}
	std::string id = newUuid();
	if (writeFileAtomic(file, id + '\n'))
		persist();
	return id;
}
} // namespace

Context currentContext(const fs::path &userDir)
{
	Context context;
	context.installId = installId(userDir);
	context.submissionId = newUuid();
	context.version = PACKAGE_VERSION;
	context.versionMinor = VERSION_MINOR;
	context.simRevision = SIM_REVISION;
	context.distribution = GLOB2_DISTRIBUTION_NAME;
	context.platform = platformName();
	context.os = operatingSystem();
	const auto &presentation = GAGCore::presentationState;
	context.deviceClass = !presentation.touch ? "desktop"
						  : presentation.layout == GAGCore::PresentationLayout::Compact ? "phone"
																						: "tablet";
	context.deviceTier = DeviceProfile::tierName(DeviceProfile::current().tier);
	context.createdAt = utcStamp("%Y-%m-%dT%H:%M:%SZ");
	return context;
}

std::string contextJson(const Context &context, const std::string &description)
{
	// The CrashReportContext schema: exactly these fields.
	const nlohmann::json json = {
		{"schema", 1},
		{"kind", "problem"},
		{"installId", context.installId},
		{"submissionId", context.submissionId},
		{"version", context.version},
		{"versionMinor", context.versionMinor},
		{"simRevision", context.simRevision},
		{"distribution", context.distribution},
		{"platform", context.platform},
		{"os", context.os},
		{"deviceClass", context.deviceClass},
		{"deviceTier", context.deviceTier},
		{"description", description},
		{"createdAt", context.createdAt},
	};
	return json.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

std::string fileName(const Context &context)
{
	// createdAt is YYYY-MM-DDTHH:MM:SSZ.
	std::string stamp;
	for (char c : context.createdAt)
		if (c >= '0' && c <= '9')
			stamp += c;
	if (stamp.size() >= 14)
		stamp = stamp.substr(0, 8) + "-" + stamp.substr(8, 6);
	return "glob2-report-" + stamp + ".zip";
}

// ---------------------------------------------------------------- zip

namespace
{
void put16(std::string &out, unsigned value)
{
	out += static_cast<char>(value & 0xFF);
	out += static_cast<char>((value >> 8) & 0xFF);
}
void put32(std::string &out, std::uint32_t value)
{
	put16(out, value & 0xFFFF);
	put16(out, value >> 16);
}
unsigned get16(const std::string &in, std::size_t at)
{
	return static_cast<unsigned char>(in[at]) | (static_cast<unsigned char>(in[at + 1]) << 8);
}
std::uint32_t get32(const std::string &in, std::size_t at)
{
	return get16(in, at) | (static_cast<std::uint32_t>(get16(in, at + 2)) << 16);
}
std::uint32_t crcOf(const std::string &bytes)
{
	uLong crc = crc32(0L, Z_NULL, 0);
	std::size_t at = 0;
	while (at < bytes.size())
	{
		const auto chunk = static_cast<uInt>(std::min<std::size_t>(bytes.size() - at, 1u << 30));
		crc = crc32(crc, reinterpret_cast<const Bytef *>(bytes.data() + at), chunk);
		at += chunk;
	}
	return static_cast<std::uint32_t>(crc);
}
// 1 January 2026, 00:00, in MS-DOS form: entries carry no real time.
constexpr unsigned DOS_TIME = 0;
constexpr unsigned DOS_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1;
} // namespace

std::string zip(const std::vector<Entry> &entries)
{
	std::string out, directory;
	for (const auto &entry : entries)
	{
		const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
		const std::uint32_t crc = crcOf(entry.bytes);
		const auto size = static_cast<std::uint32_t>(entry.bytes.size());
		put32(out, 0x04034b50);
		put16(out, 10); // version needed: stored
		put16(out, 0);  // flags
		put16(out, 0);  // stored
		put16(out, DOS_TIME);
		put16(out, DOS_DATE);
		put32(out, crc);
		put32(out, size);
		put32(out, size);
		put16(out, static_cast<unsigned>(entry.name.size()));
		put16(out, 0);
		out += entry.name;
		out += entry.bytes;

		put32(directory, 0x02014b50);
		put16(directory, 20); // made by
		put16(directory, 10);
		put16(directory, 0);
		put16(directory, 0);
		put16(directory, DOS_TIME);
		put16(directory, DOS_DATE);
		put32(directory, crc);
		put32(directory, size);
		put32(directory, size);
		put16(directory, static_cast<unsigned>(entry.name.size()));
		put16(directory, 0); // extra
		put16(directory, 0); // comment
		put16(directory, 0); // disk
		put16(directory, 0); // internal attributes
		put32(directory, 0); // external attributes
		put32(directory, offset);
		directory += entry.name;
	}
	const auto directoryOffset = static_cast<std::uint32_t>(out.size());
	out += directory;
	put32(out, 0x06054b50);
	put16(out, 0);
	put16(out, 0);
	put16(out, static_cast<unsigned>(entries.size()));
	put16(out, static_cast<unsigned>(entries.size()));
	put32(out, static_cast<std::uint32_t>(directory.size()));
	put32(out, directoryOffset);
	put16(out, 0);
	return out;
}

std::optional<std::vector<Entry>> unzip(const std::string &archive)
{
	if (archive.size() < 22)
		return std::nullopt;
	const std::size_t end = archive.size() - 22;
	if (get32(archive, end) != 0x06054b50)
		return std::nullopt;
	const unsigned count = get16(archive, end + 10);
	std::size_t at = get32(archive, end + 16);
	std::vector<Entry> entries;
	for (unsigned i = 0; i < count; ++i)
	{
		if (at + 46 > end || get32(archive, at) != 0x02014b50 || get16(archive, at + 10) != 0)
			return std::nullopt;
		const std::uint32_t crc = get32(archive, at + 16);
		const std::uint32_t size = get32(archive, at + 24);
		const unsigned nameLength = get16(archive, at + 28);
		const std::size_t skip = 46 + nameLength + get16(archive, at + 30) + get16(archive, at + 32);
		const std::uint32_t local = get32(archive, at + 42);
		if (at + skip > end || local + 30 > end || get32(archive, local) != 0x04034b50)
			return std::nullopt;
		const std::size_t data = local + 30 + get16(archive, local + 26) + get16(archive, local + 28);
		if (data + size > end)
			return std::nullopt;
		Entry entry{archive.substr(at + 46, nameLength), archive.substr(data, size)};
		if (crcOf(entry.bytes) != crc)
			return std::nullopt;
		entries.push_back(std::move(entry));
		at += skip;
	}
	return entries;
}

// ---------------------------------------------------------------- bundles

fs::path userDirectory()
{
	auto *files = GAGCore::Toolkit::getFileManager();
	// FileManager paths are UTF-8; on Windows fs::path(std::string) would read
	// them in the ANSI code page.
	return files && files->getDirCount() > 0 ? fs::u8path(files->getDir(0)) : fs::path();
}

std::optional<fs::path> newestFile(const fs::path &dir, const std::string &extension)
{
	std::error_code error;
	std::optional<fs::path> newest;
	fs::file_time_type newestTime{};
	for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error))
	{
		const auto &path = it->path();
		const std::string name = path.filename().string();
		const auto endsWith = [&name](const std::string &tail)
		{ return name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0; };
		// Saves and replays are written plain or gzip-compressed.
		if (!it->is_regular_file(error) || name.front() == '.' ||
			!(endsWith("." + extension) || endsWith("." + extension + ".gz")))
			continue;
		const auto time = it->last_write_time(error);
		if (error)
			continue;
		if (!newest || time > newestTime || (time == newestTime && path < *newest))
		{
			newest = path;
			newestTime = time;
		}
	}
	return newest;
}

Bundle build(const Choices &choices, const Context &context, const fs::path &userDir)
{
	const std::string description = clampUtf8(choices.description, DESCRIPTION_LIMIT);
	Bundle bundle;
	std::vector<Entry> entries{{"context.json", contextJson(context, description)}};
	std::size_t total = entries.front().bytes.size() + 256;
	auto attach = [&](const std::string &name, std::optional<std::string> bytes)
	{
		if (!bytes)
			return;
		if (total + bytes->size() + 128 > BUNDLE_LIMIT)
		{
			bundle.tooLarge.push_back(name);
			return;
		}
		total += bytes->size() + 128;
		entries.push_back({name, std::move(*bytes)});
	};
	// A file is weighed before it is read, so one larger than the whole
	// bundle is reported as left out, not silently skipped.
	auto attachFile = [&](const std::string &name, const fs::path &file)
	{
		std::error_code error;
		const auto size = fs::file_size(file, error);
		if (error)
			return;
		if (total + size + 128 > BUNDLE_LIMIT)
		{
			bundle.tooLarge.push_back(name);
			return;
		}
		attach(name, readFile(file, BUNDLE_LIMIT));
	};
	if (choices.log)
		attach("log.txt", recentLog());
	// A crash that may not be sent waits on the device; the player can pass it on.
	if (choices.crash)
		for (auto &entry : CrashCapture::newestCrashEntries(userDir))
			attach(entry.name, std::move(entry.bytes));
	// Attachments keep a .gz suffix when the file has one.
	const auto gz = [](const fs::path &file) { return file.extension() == ".gz" ? std::string(".gz") : std::string(); };
	if (choices.save)
		if (auto file = newestFile(userDir / "games", "game"))
			attachFile("save.game" + gz(*file), *file);
	if (choices.replay)
		if (auto file = newestFile(userDir / "replays", "replay"))
			attachFile("last.replay" + gz(*file), *file);
	for (const auto &entry : entries)
		bundle.attached.push_back(entry.name);
	bundle.bytes = zip(entries);
	return bundle;
}

// ---------------------------------------------------------------- outbox

Outbox::Outbox(fs::path userDir) : dir(std::move(userDir) / "reports" / "outbox") {}

std::vector<fs::path> Outbox::pending() const
{
	std::vector<fs::path> files;
	std::error_code error;
	for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error))
		if (it->path().extension() == ".zip" && it->is_regular_file(error))
			files.push_back(it->path());
	// Names start with the time they were queued.
	std::sort(files.begin(), files.end());
	return files;
}

void Outbox::remove(const fs::path &file)
{
	std::error_code error;
	fs::remove(file, error);
	persist();
}

bool Outbox::add(const std::string &bundle)
{
	if (bundle.size() > OUTBOX_BYTES)
		return false;
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch())
						.count();
	char name[64];
	// Process-wide, so two reports queued in the same millisecond never share a name.
	static std::atomic<unsigned> sequence{0};
	std::snprintf(name, sizeof name, "%016lld-%04u.zip", static_cast<long long>(ms), sequence++ % 10000);
	if (!writeFileAtomic(dir / name, bundle))
		return false;
	auto files = pending();
	std::uintmax_t bytes = 0;
	std::error_code error;
	for (const auto &file : files)
		bytes += fs::file_size(file, error);
	// Drop the oldest until the caps hold; the new report is the newest.
	for (std::size_t i = 0; i + 1 < files.size() && (files.size() - i > OUTBOX_COUNT || bytes > OUTBOX_BYTES); ++i)
	{
		bytes -= std::min(bytes, static_cast<std::uintmax_t>(fs::file_size(files[i], error)));
		fs::remove(files[i], error);
	}
	persist();
	return true;
}

// ---------------------------------------------------------------- upload

std::string endpoint()
{
	// Development and device checks may point reports at another glob2-online
	// (a local or staging API); http:// is accepted for loopback hosts only.
	std::string origin = Online::OFFICIAL_INSTANCE_ORIGIN;
	if (const char *value = SDL_getenv_unsafe("GLOB2_PROBLEM_REPORT_ORIGIN"); value && *value)
		if (auto normalized = Online::normalizeOrigin(value))
			origin = *normalized;
	return Online::apiUrl(origin, "/api/v1/crash-reports");
}

UploadState classify(int status)
{
	if (status >= 200 && status < 300)
		return UploadState::Sent;
	if (status == 408 || status == 429 || status >= 500)
		return UploadState::Retry;
	if (status >= 400)
		return UploadState::Refused;
	return UploadState::Retry;
}

std::optional<std::string> receiptId(const std::string &body)
{
	const auto json = nlohmann::json::parse(body, nullptr, false);
	if (!json.is_object() || !json.contains("id") || !json["id"].is_string())
		return std::nullopt;
	const auto id = json["id"].get<std::string>();
	static const std::regex pattern("^[A-HJ-NP-Z2-9]{8}$");
	if (!std::regex_match(id, pattern))
		return std::nullopt;
	return id;
}

namespace
{
class FetchUpload : public Upload
{
  public:
	explicit FetchUpload(const std::string &bundle)
	{
		HttpFetch::Request request;
		request.method = HttpFetch::Method::Post;
		request.url = endpoint();
		request.headers = {{"Content-Type", "application/zip"}};
		request.body = bundle;
		request.timeoutMilliseconds = 120000;
		request.responseLimit = 64 * 1024;
		fetch = HttpFetch::start(std::move(request));
	}
	UploadState poll() override
	{
		if (done)
			return result;
		const auto state = fetch->state();
		if (state == HttpFetch::State::Pending)
			return UploadState::Pending;
		done = true;
		if (state != HttpFetch::State::Done)
		{
			failure = fetch->error();
			return result = UploadState::Retry;
		}
		const auto &response = fetch->response();
		result = classify(response.status);
		if (result == UploadState::Sent)
		{
			if (auto id = receiptId(response.body))
				receipt = *id;
			else
			{
				failure = "unexpected answer";
				result = UploadState::Retry;
			}
		}
		else
			failure = "HTTP " + std::to_string(response.status);
		return result;
	}
	std::string id() const override { return receipt; }
	std::string error() const override { return failure; }

  private:
	std::unique_ptr<HttpFetch::Fetch> fetch;
	bool done = false;
	UploadState result = UploadState::Pending;
	std::string receipt, failure;
};

Transport &transport()
{
	static Transport value;
	return value;
}
} // namespace

void setTransport(Transport value)
{
	transport() = std::move(value);
}

std::unique_ptr<Upload> send(const std::string &bundle)
{
	if (transport())
		return transport()(bundle);
	return std::make_unique<FetchUpload>(bundle);
}

namespace
{
struct Pump
{
	std::unique_ptr<Upload> upload;
	fs::path file;
	std::optional<std::uint32_t> next;
};
Pump &pumpState()
{
	static Pump pump;
	return pump;
}
constexpr std::uint32_t FIRST_TRY_MS = 15000;
constexpr std::uint32_t RETRY_MS = 5 * 60 * 1000;
} // namespace

void resetOutboxPump()
{
	pumpState() = Pump{};
}

void pumpOutbox(std::uint32_t tick)
{
	auto &pump = pumpState();
	if (!pump.next)
		pump.next = tick + FIRST_TRY_MS;
	if (pump.upload)
	{
		const UploadState state = pump.upload->poll();
		if (state == UploadState::Pending)
			return;
		Outbox outbox(userDirectory());
		if (state == UploadState::Sent)
		{
			std::clog << "Problem report sent: " << pump.upload->id() << std::endl;
			outbox.remove(pump.file);
			pump.next = tick; // the next queued report, if any
		}
		else if (state == UploadState::Refused)
		{
			std::clog << "Problem report refused (" << pump.upload->error() << "); discarded" << std::endl;
			outbox.remove(pump.file);
			pump.next = tick;
		}
		else
			pump.next = tick + RETRY_MS;
		pump.upload.reset();
		return;
	}
	if (static_cast<std::int32_t>(tick - *pump.next) < 0)
		return;
	pump.next = tick + RETRY_MS;
	if (currentSendPolicy() != SendPolicy::Allowed)
		return;
	Outbox outbox(userDirectory());
	const auto files = outbox.pending();
	if (files.empty())
		return;
	auto bytes = readFile(files.front(), OUTBOX_BYTES);
	if (!bytes || !unzip(*bytes))
	{
		outbox.remove(files.front()); // unreadable: never retried
		pump.next = tick;
		return;
	}
	pump.file = files.front();
	pump.upload = send(*bytes);
}
} // namespace ProblemReport
