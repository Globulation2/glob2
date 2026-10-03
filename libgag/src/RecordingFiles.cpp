// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include <Toolkit.h>
#include <FileManager.h>
#include "RecordingSession.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#ifdef __EMSCRIPTEN__
#include "RecordingPlatform.h"
#include <nlohmann/json.hpp>
#endif

namespace GAGCore::Recording
{
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
	std::filesystem::path root = std::filesystem::u8path(Toolkit::getFileManager()->getDir(0)) / "videoshots";
	~Library() { if (worker.joinable()) worker.join(); }
	void run(std::function<void()> operation)
	{
		if (!finished) return;
		if (worker.joinable()) worker.join();
		finished = false;
		{ std::lock_guard lock(mutex); status.busy = true; status.error.clear(); }
		worker = std::thread([this,operation = std::move(operation)]
		{
			try { operation(); }
			catch (const std::exception &e) { std::lock_guard lock(mutex); status.error = e.what(); }
			{ std::lock_guard lock(mutex); status.busy = false; }
			finished = true;
		});
	}
	void scan()
	{
		std::vector<RecordingFileEntry> files;
		if (std::filesystem::is_directory(root)) for (const auto &entry : std::filesystem::directory_iterator(root))
		{
			auto p = entry.path(); auto value = p.u8string();
			std::string path(reinterpret_cast<const char *>(value.data()),value.size());
			if (entry.is_regular_file() && p.extension() == ".mp4" && std::filesystem::exists(std::filesystem::u8path(path+".json"))) files.push_back({path,false});
			if (entry.is_directory() && p.extension() == ".recording" && std::filesystem::exists(p/"manifest.json")) files.push_back({path.substr(0,path.size()-10),true});
		}
		for (const auto &path : recorder().status().outputs)
			if (std::filesystem::exists(std::filesystem::u8path(path)) && std::none_of(files.begin(),files.end(),[&](const auto &entry) { return entry.path == path; })) files.push_back({path,false});
		std::sort(files.begin(),files.end(),[](const auto &a,const auto &b) { return a.path > b.path; });
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
		for (const auto &file : value["files"]) result.files.push_back({file.at("path").get<std::string>(),file.value("recoverable",false)}); }
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
