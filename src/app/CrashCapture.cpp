// SPDX-License-Identifier: GPL-3.0-or-later
#include "CrashCapture.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// The platform layers define these: platforms/mobile/android/CrashCapture.cpp
// and platforms/mobile/ios/CrashCapture.mm. Other builds capture nothing yet.
// (iOS links the game from a static archive, where a weak default here would
// keep the MetricKit layer's object out of the app.)
extern "C" bool glob2_crash_capture_built(void);
extern "C" void glob2_crash_capture_start(const char *userDir);
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
extern "C" bool glob2_crash_capture_built(void) { return false; }
extern "C" void glob2_crash_capture_start(const char *) {}
#endif

namespace CrashCapture
{
bool built()
{
	return ProblemReport::sendingBuilt() && glob2_crash_capture_built();
}

// ---------------------------------------------------------------- session file

namespace
{
// The session file: this header, then a ring of log bytes. Everything is
// written in place through a shared mapping, so a crash loses nothing that
// was written before it.
struct Header
{
	char magic[8];
	std::uint32_t version;
	std::uint32_t capacity;     // ring bytes
	std::uint64_t written;      // bytes ever written; the ring holds the last `capacity`
	std::int64_t startedAt;     // Unix seconds
	std::uint32_t matchRunning; // 1 while a match is shown
	std::uint32_t matchTick;
	std::uint32_t matchSeed;
	std::uint32_t reserved[5];
};
static_assert(sizeof(Header) == 64);
constexpr char MAGIC[8] = {'G', '2', 'S', 'E', 'S', 'S', 'N', '1'};
constexpr std::size_t RING_BYTES = SESSION_FILE_BYTES - sizeof(Header);

std::mutex sessionMutex;
Header *mapped = nullptr; // written under sessionMutex
std::atomic<Header *> tickTarget{nullptr}; // matchTick's lock-free view of it
#if defined(_WIN32)
HANDLE fileHandle = INVALID_HANDLE_VALUE;
HANDLE mappingHandle = nullptr;
#endif

char *ring()
{
	return reinterpret_cast<char *>(mapped) + sizeof(Header);
}

std::int64_t now()
{
	return std::chrono::duration_cast<std::chrono::seconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

void *mapFile(const fs::path &file)
{
#if defined(__EMSCRIPTEN__)
	(void)file;
	return nullptr; // the browser reports errors as they happen
#elif defined(_WIN32)
	fileHandle = CreateFileW(file.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
							 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (fileHandle == INVALID_HANDLE_VALUE)
		return nullptr;
	mappingHandle = CreateFileMappingW(fileHandle, nullptr, PAGE_READWRITE, 0, DWORD(SESSION_FILE_BYTES), nullptr);
	void *view = mappingHandle ? MapViewOfFile(mappingHandle, FILE_MAP_WRITE, 0, 0, SESSION_FILE_BYTES) : nullptr;
	if (!view)
	{
		if (mappingHandle)
			CloseHandle(mappingHandle);
		CloseHandle(fileHandle);
		mappingHandle = nullptr;
		fileHandle = INVALID_HANDLE_VALUE;
	}
	return view;
#else
	const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return nullptr;
	void *view = nullptr;
	if (::ftruncate(fd, static_cast<off_t>(SESSION_FILE_BYTES)) == 0)
	{
		view = ::mmap(nullptr, SESSION_FILE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (view == MAP_FAILED)
			view = nullptr;
	}
	::close(fd); // the mapping keeps the file
	return view;
#endif
}

void unmapFile()
{
	tickTarget.store(nullptr, std::memory_order_release);
#if defined(_WIN32)
	UnmapViewOfFile(mapped);
	CloseHandle(mappingHandle);
	CloseHandle(fileHandle);
	mappingHandle = nullptr;
	fileHandle = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__)
	::munmap(mapped, SESSION_FILE_BYTES);
#endif
	mapped = nullptr;
}

// A release store into the mapped file (the NDK's libc++ has no std::atomic_ref).
template <typename T> void publish(T &field, T value)
{
	__atomic_store_n(&field, value, __ATOMIC_RELEASE);
}
} // namespace

bool startSession(const fs::path &userDir)
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	if (mapped)
		unmapFile();
	std::error_code error;
	const fs::path dir = userDir / "reports";
	fs::create_directories(dir, error);
	const fs::path file = dir / "session.log";
	if (fs::exists(file, error))
		fs::rename(file, dir / "previous-session.log", error);
	mapped = static_cast<Header *>(mapFile(file));
	if (!mapped)
		return false;
	std::memset(mapped, 0, SESSION_FILE_BYTES);
	mapped->version = 1;
	mapped->capacity = static_cast<std::uint32_t>(RING_BYTES);
	mapped->startedAt = now();
	std::memcpy(mapped->magic, MAGIC, sizeof MAGIC); // last: marks the header complete
	tickTarget.store(mapped, std::memory_order_release);
	return true;
}

void stopSession()
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	if (mapped)
		unmapFile();
}

void mirrorLine(std::string_view line)
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	if (!mapped)
		return;
	std::uint64_t at = mapped->written;
	auto put = [&](const char *bytes, std::size_t size)
	{
		if (size > RING_BYTES)
		{
			bytes += size - RING_BYTES;
			size = RING_BYTES;
		}
		const std::size_t start = static_cast<std::size_t>(at % RING_BYTES);
		const std::size_t first = std::min(size, RING_BYTES - start);
		std::memcpy(ring() + start, bytes, first);
		std::memcpy(ring(), bytes + first, size - first);
		at += size;
	};
	put(line.data(), line.size());
	put("\n", 1);
	publish(mapped->written, at);
}

void matchStarted(std::uint32_t seed)
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	if (!mapped)
		return;
	publish(mapped->matchSeed, seed);
	publish(mapped->matchTick, std::uint32_t(0));
	publish(mapped->matchRunning, std::uint32_t(1));
}

void matchTick(std::uint32_t tick)
{
	// Called every drawn frame of a match: no lock, one store.
	if (Header *header = tickTarget.load(std::memory_order_acquire))
		publish(header->matchTick, tick);
}

void matchEnded()
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	if (mapped)
		publish(mapped->matchRunning, std::uint32_t(0));
}

std::optional<Session> readSession(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	std::string bytes(SESSION_FILE_BYTES, '\0');
	if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
		return std::nullopt;
	Header header;
	std::memcpy(&header, bytes.data(), sizeof header);
	if (std::memcmp(header.magic, MAGIC, sizeof MAGIC) != 0 || header.version != 1 ||
		header.capacity != RING_BYTES)
		return std::nullopt;
	const char *data = bytes.data() + sizeof(Header);
	std::string log;
	if (header.written <= RING_BYTES)
		log.assign(data, static_cast<std::size_t>(header.written));
	else
	{
		const std::size_t start = static_cast<std::size_t>(header.written % RING_BYTES);
		log.assign(data + start, RING_BYTES - start);
		log.append(data, start);
		// The oldest line was cut by the ring; drop it.
		const auto newline = log.find('\n');
		log.erase(0, newline == std::string::npos ? log.size() : newline + 1);
	}
	Session session;
	session.log = ProblemReport::clampUtf8(log, log.size());
	session.startedAt = header.startedAt;
	if (header.matchRunning == 1)
		session.match = Match{header.matchTick, header.matchSeed};
	return session;
}

// ---------------------------------------------------------------- crashes

const char *kindName(Kind kind)
{
	switch (kind)
	{
	case Kind::Minidump:
		return "crash";
	case Kind::MetricKit:
		return "metrickit";
	case Kind::Java:
		return "java";
	}
	return "crash";
}

const char *entryName(Kind kind)
{
	switch (kind)
	{
	case Kind::Minidump:
		return "minidump.dmp";
	case Kind::MetricKit:
		return "metrickit.json";
	case Kind::Java:
		return "java-exception.txt";
	}
	return "minidump.dmp";
}

const char *fileSuffix(Kind kind)
{
	switch (kind)
	{
	case Kind::Minidump:
		return ".dmp";
	case Kind::MetricKit:
		return ".metrickit.json";
	case Kind::Java:
		return ".java.txt";
	}
	return ".dmp";
}

namespace
{
std::atomic<bool> arrived{false};

std::string randomName()
{
	std::random_device entropy;
	static const char *hex = "0123456789abcdef";
	std::string name;
	for (int i = 0; i < 32; ++i)
		name += hex[entropy() & 15];
	return name;
}

std::optional<Kind> kindOf(const fs::path &file)
{
	const std::string name = ProblemReport::utf8(file.filename());
	for (Kind kind : {Kind::MetricKit, Kind::Java, Kind::Minidump})
	{
		const std::string suffix = fileSuffix(kind);
		if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
			return kind;
	}
	return std::nullopt;
}

// The crash's file without its suffix, plus ".log".
fs::path logOf(const Crash &crash)
{
	std::string name = ProblemReport::utf8(crash.file.filename());
	name.resize(name.size() - std::strlen(fileSuffix(crash.kind)));
	return crash.file.parent_path() / fs::u8path(name + ".log");
}

std::optional<std::string> readAll(const fs::path &path, std::uintmax_t limit)
{
	std::error_code error;
	const auto size = fs::file_size(path, error);
	if (error || size > limit)
		return std::nullopt;
	std::ifstream in(path, std::ios::binary);
	std::string bytes(static_cast<std::size_t>(size), '\0');
	if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
		return std::nullopt;
	return bytes;
}

bool writeAll(const fs::path &path, const std::string &bytes)
{
	const fs::path temporary = path.string() + ".part";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
			return false;
	}
	std::error_code error;
	fs::rename(temporary, path, error);
	if (error)
		fs::remove(temporary, error);
	return !error;
}

std::int64_t timeOf(const fs::path &file)
{
	std::error_code error;
	const auto time = fs::last_write_time(file, error);
	if (error)
		return 0;
	const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
	return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

// Every crash on disk, newest first.
std::vector<Crash> scan(const fs::path &userDir)
{
	std::vector<Crash> crashes;
	std::error_code error;
	for (fs::directory_iterator it(crashesDirectory(userDir), error), end; !error && it != end; it.increment(error))
	{
		if (!it->is_regular_file(error))
			continue;
		if (auto kind = kindOf(it->path()))
			crashes.push_back({it->path(), *kind, timeOf(it->path())});
	}
	std::sort(crashes.begin(), crashes.end(), [](const Crash &a, const Crash &b)
			  { return a.capturedAt != b.capturedAt ? a.capturedAt > b.capturedAt : a.file > b.file; });
	return crashes;
}

// Keeps the newest crashes within the caps.
void prune(std::vector<Crash> &crashes)
{
	std::uintmax_t total = 0;
	std::size_t kept = 0;
	for (auto it = crashes.begin(); it != crashes.end();)
	{
		std::error_code error;
		const auto size = fs::file_size(it->file, error) + fs::file_size(logOf(*it), error);
		if (kept < CRASHES_COUNT && total + size <= CRASHES_BYTES)
		{
			total += size;
			++kept;
			++it;
		}
		else
		{
			discard(*it);
			it = crashes.erase(it);
		}
	}
}
} // namespace

fs::path crashesDirectory(const fs::path &userDir)
{
	return userDir / "reports" / "crashes";
}

bool store(const fs::path &userDir, Kind kind, const std::string &bytes)
{
	if (bytes.empty() || bytes.size() > CRASHES_BYTES)
		return false;
	std::error_code error;
	fs::create_directories(crashesDirectory(userDir), error);
	const fs::path file = crashesDirectory(userDir) / fs::u8path(randomName() + fileSuffix(kind));
	if (!writeAll(file, bytes))
		return false;
	auto crashes = scan(userDir);
	prune(crashes);
	return true;
}

std::vector<Crash> pending(const fs::path &userDir)
{
	auto crashes = scan(userDir);
	prune(crashes);
	// The previous session's log belongs to the crash that ended it: the newest
	// crash captured since that session started, and only once. It waits until
	// such a crash appears, since MetricKit delivers some time after launch; the
	// next run's session replaces it.
	const fs::path previous = userDir / "reports" / "previous-session.log";
	std::error_code error;
	if (crashes.empty() || fs::exists(logOf(crashes.front()), error) || !fs::exists(previous, error))
		return crashes;
	const auto session = readSession(previous);
	if (!session)
		fs::remove(previous, error);
	else if (crashes.front().capturedAt + 1 >= session->startedAt)
	{
		nlohmann::json log = {{"log", session->log}};
		if (session->match)
			log["match"] = {{"tick", session->match->tick}, {"seed", session->match->seed}};
		if (writeAll(logOf(crashes.front()), log.dump()))
			fs::remove(previous, error);
	}
	return crashes;
}

void discard(const Crash &crash)
{
	std::error_code error;
	fs::remove(crash.file, error);
	fs::remove(logOf(crash), error);
}

std::string contextJson(const ProblemReport::Context &context, Kind kind, const std::optional<Match> &match)
{
	auto json = nlohmann::json::parse(ProblemReport::contextJson(context, ""));
	json["kind"] = kindName(kind);
	if (match)
		json["match"] = {{"tick", match->tick}, {"seed", match->seed}};
	return json.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

namespace
{
struct CrashLog
{
	std::string text;
	std::optional<Match> match;
};

CrashLog logFor(const Crash &crash)
{
	CrashLog result;
	const auto stored = readAll(logOf(crash), 2 * SESSION_FILE_BYTES);
	if (!stored)
		return result;
	const auto json = nlohmann::json::parse(*stored, nullptr, false);
	if (!json.is_object())
		return result;
	if (json.contains("log") && json["log"].is_string())
		result.text = json["log"].get<std::string>();
	if (json.contains("match") && json["match"].is_object())
	{
		const auto &m = json["match"];
		if (m.contains("tick") && m["tick"].is_number_unsigned() && m.contains("seed") && m["seed"].is_number_unsigned())
			result.match = Match{m["tick"].get<std::uint32_t>(), m["seed"].get<std::uint32_t>()};
	}
	return result;
}
} // namespace

std::string bundle(const Crash &crash, const ProblemReport::Context &context)
{
	const auto payload = readAll(crash.file, CRASHES_BYTES);
	if (!payload || payload->empty())
		return {};
	const CrashLog log = logFor(crash);
	std::vector<ProblemReport::Entry> entries{{"context.json", contextJson(context, crash.kind, log.match)},
											  {entryName(crash.kind), *payload}};
	if (!log.text.empty())
		entries.push_back({"log.txt", log.text});
	return ProblemReport::zip(entries);
}

std::vector<ProblemReport::Entry> newestCrashEntries(const fs::path &userDir)
{
	const auto crashes = pending(userDir);
	if (crashes.empty())
		return {};
	const auto payload = readAll(crashes.front().file, CRASHES_BYTES);
	if (!payload)
		return {};
	std::vector<ProblemReport::Entry> entries{{entryName(crashes.front().kind), *payload}};
	const CrashLog log = logFor(crashes.front());
	if (!log.text.empty())
		entries.push_back({"crash-log.txt", log.text});
	return entries;
}

// ---------------------------------------------------------------- consent

Action decide(ProblemReport::SendPolicy policy, Consent consent)
{
	if (policy != ProblemReport::SendPolicy::Allowed)
		return Action::KeepLocal;
	return consent == Consent::Always ? Action::Send : Action::Ask;
}

std::size_t queueAll(const fs::path &userDir)
{
	ProblemReport::Outbox outbox(userDir);
	std::size_t queued = 0;
	const auto crashes = pending(userDir);
	// Oldest first, so the outbox drops the oldest if it overflows.
	for (auto it = crashes.rbegin(); it != crashes.rend(); ++it)
	{
		const std::string bytes = bundle(*it, ProblemReport::currentContext(userDir));
		if (!bytes.empty() && outbox.add(bytes))
			++queued;
		discard(*it);
	}
	return queued;
}

void discardAll(const fs::path &userDir)
{
	for (const auto &crash : pending(userDir))
		discard(crash);
}

void noteArrival()
{
	arrived.store(true, std::memory_order_release);
}

bool takeArrival()
{
	return arrived.exchange(false, std::memory_order_acq_rel);
}

// ---------------------------------------------------------------- platform

void startPlatform(const fs::path &userDir)
{
	if (!built())
		return;
	startSession(userDir);
	glob2_crash_capture_start(ProblemReport::utf8(userDir).c_str());
}

#if defined(__clang__)
__attribute__((noinline, optnone))
#elif defined(__GNUC__)
__attribute__((noinline, optimize("O0")))
#endif
void crashForTest()
{
	// A write through a null pointer: a real SIGSEGV (EXC_BAD_ACCESS on iOS) in
	// this function, which is what a player's crash looks like.
	volatile int *nowhere = nullptr;
	*nowhere = 0x6c6f6232;
	std::abort();
}
} // namespace CrashCapture
