// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include "ThreadSupport.h"
#include <Toolkit.h>
#include <FileManager.h>
#include "RecordingSession.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#ifdef __EMSCRIPTEN__
#include "RecordingPlatform.h"
#endif

namespace GAGCore::Recording
{
#ifndef __EMSCRIPTEN__
namespace
{
std::string utf8(const std::filesystem::path &path)
{
	auto value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}
std::int64_t unixSeconds(std::filesystem::file_time_type time)
{
	// file_clock has no portable epoch before C++20 clock_cast; translate through now.
	const auto system = std::chrono::system_clock::now() +
		std::chrono::duration_cast<std::chrono::system_clock::duration>(time - std::filesystem::file_time_type::clock::now());
	return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}
std::int64_t manifestDuration(const std::filesystem::path &manifest)
{
	std::ifstream input(manifest, std::ios::binary);
	if (!input)
		return 0;
	const auto value = nlohmann::json::parse(input, nullptr, false);
	return value.is_object() ? value.value("duration_us", std::int64_t(0)) : 0;
}
} // namespace
std::vector<RecordingFileEntry> scanRecordings(const std::string &directory)
{
	std::vector<RecordingFileEntry> files;
	const auto root = std::filesystem::u8path(directory);
	std::error_code error;
	if (!std::filesystem::is_directory(root, error))
		return files;
	for (const auto &entry : std::filesystem::directory_iterator(root, error))
	{
		const auto p = entry.path();
		auto withSuffix = [&](const char *suffix) { auto value = p; return value += suffix; };
		if (entry.is_regular_file(error) && (p.extension() == ".mp4" || p.extension() == ".webm") &&
			std::filesystem::exists(withSuffix(".json"), error))
		{
			RecordingFileEntry file{utf8(p), false};
			file.bytes = entry.file_size(error);
			file.modified = unixSeconds(entry.last_write_time(error));
			file.durationUs = manifestDuration(withSuffix(".json"));
			files.push_back(std::move(file));
		}
		else if (entry.is_directory(error) && p.extension() == ".recording" && std::filesystem::exists(p / "manifest.json", error))
		{
			auto video = p;
			video.replace_extension();
			RecordingFileEntry file{utf8(video), true};
			file.modified = unixSeconds(std::filesystem::last_write_time(p / "manifest.json", error));
			files.push_back(std::move(file));
		}
	}
	// Newest first by time, never by name: names from older versions do not sort by date.
	std::sort(files.begin(), files.end(), [](const auto &a, const auto &b)
			  { return a.modified != b.modified ? a.modified > b.modified : a.path > b.path; });
	return files;
}
#endif
namespace
{
std::atomic<bool> requested{false};
#ifndef __EMSCRIPTEN__
struct Library
{
	std::mutex mutex;
	RecordingFilesStatus status;
	std::thread worker;
	std::atomic<bool> finished{true};
	~Library() { if (worker.joinable()) worker.join(); }
	void run(std::function<void()> operation)
	{
		if (!finished) return;
		if (worker.joinable()) worker.join();
		finished = false;
		{ std::lock_guard lock(mutex); status.busy = true; status.error.clear(); }
		worker = std::thread([this,operation = std::move(operation)]
		{
			GAGCore::ThreadSupport::setCurrentThreadRole(GAGCore::ThreadSupport::ThreadRole::Worker);
			try { operation(); }
			catch (const std::exception &e) { std::lock_guard lock(mutex); status.error = e.what(); }
			{ std::lock_guard lock(mutex); status.busy = false; }
			finished = true;
		});
	}
	void scan()
	{
		auto files = scanRecordings(recordingDirectory());
		// Outputs of a --record path outside the profile folder are listed too.
		for (const auto &path : recorder().status().outputs)
			if (std::filesystem::exists(std::filesystem::u8path(path)) && std::none_of(files.begin(),files.end(),[&](const auto &entry) { return entry.path == path; }))
				files.insert(files.begin(), {path,false});
		std::lock_guard lock(mutex); status.files = std::move(files);
	}
};
Library &library() { static Library instance; return instance; }
#endif
bool recordingBusy()
{
	auto state = recorder().status().state;
	return state == State::Starting || state == State::Recording || state == State::Finalizing;
}
}
void requestFiles() { requested = true; }
bool takeFilesRequest() { return requested.exchange(false); }
void refreshFiles()
{
#ifdef __EMSCRIPTEN__
	browserRecordingFilesRefresh();
#else
	library().run([] { library().scan(); });
#endif
}
RecordingFilesStatus filesStatus()
{
#ifdef __EMSCRIPTEN__
	char *data = browserRecordingFilesStatus();
	RecordingFilesStatus result;
	try { auto value = nlohmann::json::parse(data); result.busy = value.value("busy",false); result.error = value.value("error","");
		for (const auto &file : value["files"])
			result.files.push_back({file.at("path").get<std::string>(),file.value("recoverable",false),file.value("bytes",std::uint64_t(0)),
				std::int64_t(file.value("modified",0.0)/1000),file.value("durationUs",std::int64_t(0))}); }
	catch (...) { result.error = "Cannot read browser recording files"; }
	std::free(data); return result;
#else
	std::lock_guard lock(library().mutex); return library().status;
#endif
}
void recoverFile(const std::string &path)
{
	if (recordingBusy()) return;
#ifdef __EMSCRIPTEN__
	browserRecordingFilesRecover(path.c_str());
#else
	library().run([path] { Detail::recoverRecording(path,Detail::nativeSessionStorage()); library().scan(); });
#endif
}
void deleteFile(const std::string &path)
{
	if (recordingBusy()) return;
#ifdef __EMSCRIPTEN__
	browserRecordingFilesRemove(path.c_str());
#else
	library().run([path]
	{
		for (const auto &suffix : {"", ".json", ".events.jsonl", ".session.json"}) std::filesystem::remove(std::filesystem::u8path(path+suffix));
		std::filesystem::remove_all(std::filesystem::u8path(path+".recording"));
		library().scan();
	});
#endif
}
} // namespace GAGCore::Recording
