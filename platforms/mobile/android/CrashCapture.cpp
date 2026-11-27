// SPDX-License-Identifier: GPL-3.0-or-later
//
// Android crash capture (src/app/CrashCapture.h). Crashpad's handler is packaged
// as lib/<abi>/libcrashpad_handler.so, so Android extracts it with the other
// native libraries into a directory it may execute from; the game starts it
// only when it crashes (StartHandlerAtCrash). It writes a minidump into
// reports/crashpad/, and the next launch moves it to reports/crashes/. Uncaught
// Java exceptions go to reports/crashes/ through Glob2Activity.captureJavaCrashes.
//
// Builds without problem reports (F-Droid) compile Crashpad out
// (GLOB2_CRASHPAD=0) and capture nothing.
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "CrashCapture.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#ifndef GLOB2_CRASHPAD
#define GLOB2_CRASHPAD 0
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

#if GLOB2_CRASHPAD
#include <client/crash_report_database.h>
#include <client/crashpad_client.h>
#include <client/settings.h>
#endif

namespace fs = std::filesystem;

namespace
{
// Calls a static Glob2Activity method taking (Activity) or (String); returns its
// String result, if any.
std::string callActivity(const char *name, const char *signature, const std::string *argument)
{
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
	jclass type = activity ? env->GetObjectClass(activity) : nullptr;
	jmethodID method = type ? env->GetStaticMethodID(type, name, signature) : nullptr;
	std::string result;
	if (method)
	{
		if (argument)
		{
			jstring text = env->NewStringUTF(argument->c_str());
			env->CallStaticVoidMethod(type, method, text);
			env->DeleteLocalRef(text);
		}
		else if (auto value = static_cast<jstring>(env->CallStaticObjectMethod(type, method, activity)))
		{
			if (const char *chars = env->GetStringUTFChars(value, nullptr))
			{
				result = chars;
				env->ReleaseStringUTFChars(value, chars);
			}
			env->DeleteLocalRef(value);
		}
	}
	if (env && env->ExceptionCheck())
		env->ExceptionClear();
	if (type)
		env->DeleteLocalRef(type);
	if (activity)
		env->DeleteLocalRef(activity);
	return result;
}

#if GLOB2_CRASHPAD
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
#endif
} // namespace

extern "C" bool glob2_crash_capture_built(void)
{
	return true;
}

extern "C" void glob2_crash_capture_start(const char *userDirText)
{
	const fs::path userDir = fs::u8path(userDirText);
	const std::string crashes = CrashCapture::crashesDirectory(userDir).string();
	callActivity("captureJavaCrashes", "(Ljava/lang/String;)V", &crashes);
#if GLOB2_CRASHPAD
	const fs::path database = userDir / "reports" / "crashpad";
	std::error_code error;
	fs::create_directories(database, error);
	collectMinidumps(userDir, database);
	const std::string libraries = callActivity("nativeLibraryDirectory", "(Landroid/app/Activity;)Ljava/lang/String;", nullptr);
	if (libraries.empty())
		return;
	const fs::path handler = fs::path(libraries) / "libcrashpad_handler.so";
	if (!fs::exists(handler, error))
	{
		SDL_Log("Crash capture: no Crashpad handler at %s", handler.c_str());
		return;
	}
	// The handler is started at crash time with this process's environment; it
	// needs libc++_shared.so from the same directory.
	setenv("LD_LIBRARY_PATH", libraries.c_str(), 1);
	static crashpad::CrashpadClient client;
	const std::map<std::string, std::string> annotations{{"prod", "glob2"}, {"ver", PACKAGE_VERSION}};
	const std::vector<std::string> arguments{"--no-rate-limit"};
	if (!client.StartHandlerAtCrash(base::FilePath(handler.string()), base::FilePath(database.string()),
									base::FilePath(database.string()), std::string(), annotations, arguments))
		SDL_Log("Crash capture: Crashpad did not start");
#endif
}
