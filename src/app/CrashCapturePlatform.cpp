// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crash capture on Windows, Linux and the browser (src/app/CrashCapture.h).
// Android and Apple platforms have their own layers
// (platforms/mobile/android/CrashCapture.cpp, platforms/apple/CrashCapture.mm),
// which define these functions instead.
//
//   Windows  An unhandled-exception filter (and SIGABRT) wakes a thread made at
//            startup, which writes a minidump of the crashed thread with
//            dbghelp's MiniDumpWriteDump into reports/crashes/. Writing from
//            another thread also works when the crash used up the stack.
//   Linux    Crashpad's handler, shipped beside the game as crashpad_handler,
//            started only when the game crashes; the next launch moves its
//            minidump out of reports/crashpad/ (only builds with GLOB2_CRASHPAD).
//   Browser  shell.html catches window errors, unhandled rejections and
//            Emscripten's abort and stores a web-error.json; this file only
//            says that the build captures.
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "CrashCapture.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(__ANDROID__) && !defined(__APPLE__)

#include <filesystem>
#include <string>

#ifndef GLOB2_CRASHPAD
#define GLOB2_CRASHPAD 0
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

namespace fs = std::filesystem;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>
#include <random>

namespace
{
std::wstring dumpDirectory;
HANDLE wake = nullptr, done = nullptr;
EXCEPTION_POINTERS *pending = nullptr;
DWORD pendingThread = 0;
LONG claimed = 0;

std::wstring dumpName()
{
	std::random_device entropy;
	wchar_t name[40];
	std::swprintf(name, 40, L"%08x%08x%08x%08x.dmp", entropy(), entropy(), entropy(), entropy());
	return name;
}

DWORD WINAPI writer(void *)
{
	WaitForSingleObject(wake, INFINITE);
	const std::wstring part = dumpDirectory + L"\\" + dumpName() + L".part";
	HANDLE file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION exception{pendingThread, pending, FALSE};
		const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
													 MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
		const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
											   pending ? &exception : nullptr, nullptr, nullptr);
		CloseHandle(file);
		if (written)
			MoveFileExW(part.c_str(), part.substr(0, part.size() - 5).c_str(), MOVEFILE_REPLACE_EXISTING);
		else
			DeleteFileW(part.c_str());
	}
	SetEvent(done);
	return 0;
}

// Hands the crash to the writer thread once, and waits for the dump.
void capture(EXCEPTION_POINTERS *pointers)
{
	if (InterlockedExchange(&claimed, 1) != 0)
		return;
	pending = pointers;
	pendingThread = GetCurrentThreadId();
	SetEvent(wake);
	WaitForSingleObject(done, 30000);
}

LONG WINAPI unhandled(EXCEPTION_POINTERS *pointers)
{
	capture(pointers);
	return EXCEPTION_CONTINUE_SEARCH;
}

void aborted(int)
{
	// abort() and std::terminate: no SEH exception, so record this thread's context.
	CONTEXT context{};
	RtlCaptureContext(&context);
	EXCEPTION_RECORD record{};
	record.ExceptionCode = 0x40000015; // STATUS_FATAL_APP_EXIT
	record.ExceptionAddress = reinterpret_cast<void *>(context.
#if defined(_M_X64) || defined(__x86_64__)
		Rip
#elif defined(_M_ARM64) || defined(__aarch64__)
		Pc
#else
		Eip
#endif
	);
	EXCEPTION_POINTERS pointers{&record, &context};
	capture(&pointers);
	std::signal(SIGABRT, SIG_DFL);
	std::abort();
}
} // namespace

extern "C" bool glob2_crash_capture_built(void)
{
	return true;
}

extern "C" void glob2_crash_capture_start(const char *userDir)
{
	if (wake)
		return;
	const fs::path directory = CrashCapture::crashesDirectory(fs::u8path(userDir));
	std::error_code error;
	fs::create_directories(directory, error);
	dumpDirectory = directory.wstring();
	wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!wake || !done || !CreateThread(nullptr, 64 * 1024, writer, nullptr, 0, nullptr))
		return;
	SetUnhandledExceptionFilter(unhandled);
	std::signal(SIGABRT, aborted);
}

#elif defined(__linux__) && GLOB2_CRASHPAD
#include <client/crash_report_database.h>
#include <client/crashpad_client.h>
#include <client/settings.h>
#include <SDL3/SDL_log.h>
#include <fstream>
#include <iterator>
#include <map>
#include <unistd.h>
#include <vector>

namespace
{
// Moves the minidumps of earlier runs out of Crashpad's database.
void collectMinidumps(const fs::path &userDir, const fs::path &database)
{
	auto reports = crashpad::CrashReportDatabase::Initialize(base::FilePath(database.string()));
	if (!reports)
		return;
	if (auto *settings = reports->GetSettings())
		settings->SetUploadsEnabled(false); // the game uploads them itself
	std::vector<crashpad::CrashReportDatabase::Report> found, completed;
	reports->GetPendingReports(&found);
	reports->GetCompletedReports(&completed);
	found.insert(found.end(), completed.begin(), completed.end());
	for (const auto &report : found)
	{
		std::ifstream in(report.file_path.value(), std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CrashCapture::store(userDir, CrashCapture::Kind::Minidump, bytes);
		reports->DeleteReport(report.uuid);
	}
	reports->CleanDatabase(0);
}

fs::path executableDirectory()
{
	std::error_code error;
	return fs::read_symlink("/proc/self/exe", error).parent_path();
}
} // namespace

extern "C" bool glob2_crash_capture_built(void)
{
	return true;
}

extern "C" void glob2_crash_capture_start(const char *userDirText)
{
	const fs::path userDir = fs::u8path(userDirText);
	const fs::path database = userDir / "reports" / "crashpad";
	std::error_code error;
	fs::create_directories(database, error);
	collectMinidumps(userDir, database);
	// Packages put the handler beside the game (bin/) or in its library
	// directory (lib/glob2/), as buildsys/cmake_generate.py installs it.
	const fs::path here = executableDirectory();
	fs::path handler;
	for (const fs::path &candidate : {here / "crashpad_handler", here.parent_path() / "lib/glob2/crashpad_handler"})
		if (::access(candidate.c_str(), X_OK) == 0)
		{
			handler = candidate;
			break;
		}
	if (handler.empty())
	{
		SDL_Log("Crash capture: no Crashpad handler beside %s", here.c_str());
		return;
	}
	static crashpad::CrashpadClient client;
	const std::map<std::string, std::string> annotations{{"prod", "glob2"}, {"ver", PACKAGE_VERSION}};
	if (!client.StartHandlerAtCrash(base::FilePath(handler.string()), base::FilePath(database.string()),
									base::FilePath(database.string()), std::string(), annotations,
									{"--no-rate-limit"}))
		SDL_Log("Crash capture: Crashpad did not start");
}

#elif defined(__EMSCRIPTEN__)

extern "C" bool glob2_crash_capture_built(void)
{
	return true;
}

// shell.html installs the hooks before the game starts (glob2CrashCapture);
// they write web-error.json files into reports/crashes/.
extern "C" void glob2_crash_capture_start(const char *) {}

#else

// Linux without Crashpad (development builds, distribution packages) and other
// platforms capture nothing.
extern "C" bool glob2_crash_capture_built(void)
{
	return false;
}
extern "C" void glob2_crash_capture_start(const char *) {}

#endif
#endif
