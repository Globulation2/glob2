// SPDX-License-Identifier: GPL-3.0-or-later
#include <glob2/BuildConfig.h>
#include <GameplayRecording.h>
#include "RecordingMetadata.h"
#include "RecordingSession.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__EMSCRIPTEN__))
#include <time.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __EMSCRIPTEN__
#include "RecordingPlatform.h"
#include <nlohmann/json.hpp>
#endif

namespace GAGCore::Recording
{
namespace
{
constexpr std::size_t MaxQueuedFrames = 3;
constexpr std::size_t MaxQueuedEvents = 1024;
constexpr std::size_t AudioBlockSamples = 4096;
constexpr std::size_t AudioSampleBudget = AudioSampleRate * AudioChannels * 2;
std::int64_t now()
{
	// Recording time includes OS suspend; engine scheduling retains its own clock.
#if defined(__APPLE__)
	return std::int64_t(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)/1000);
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
	timespec time{};
	if (!clock_gettime(CLOCK_BOOTTIME,&time)) return std::int64_t(time.tv_sec)*1000000+time.tv_nsec/1000;
#elif defined(_WIN32)
	using InterruptClock = void (WINAPI *)(PULONGLONG);
	static const auto precise = reinterpret_cast<InterruptClock>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"QueryInterruptTimePrecise"));
	if (precise) { ULONGLONG time; precise(&time); return std::int64_t(time/10); }
	return std::int64_t(GetTickCount64())*1000;
#endif
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string utf8(const std::filesystem::path &path)
{
	auto value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}
using Detail::Context;
using Frame = Detail::CaptureFrame;
struct Event
{
	std::int64_t time;
	std::string kind, value;
};
struct Audio
{
	std::int64_t time = 0;
	std::size_t count = 0;
	std::array<std::int16_t, AudioBlockSamples> samples{};
};
} // namespace
struct Recorder::Impl
{
	std::atomic<State> state{State::Idle};
	std::atomic<bool> finished{true};
	std::atomic<std::int64_t> epoch{0}, stoppedAt{0}, lastCapture{-1};
	std::atomic<std::uint64_t> droppedFrames{0}, droppedAudio{0};
	std::atomic<int> captureFps{30};
	std::atomic<std::size_t> queuedFrames{0};
	std::mutex mutex, audioMutex;
	std::condition_variable wake;
	std::deque<Frame> frames;
	std::deque<Event> events;
	// Preallocated callback storage, with a separate sample budget of two seconds.
	std::array<Audio, 128> audioBlocks;
	std::size_t audioRead = 0, audioWrite = 0, audioCount = 0, audioSamples = 0;
	std::string path, error;
	Status published;
	Context context;
	std::uint64_t matchSerial = 0;
	std::thread worker;

	bool active() const
	{
		auto s = state.load();
		return s == State::Starting || s == State::Recording;
	}
	void fail(const std::string &message)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			error = message;
			stoppedAt = now();
			state = State::Failed;
		}
		wake.notify_all();
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Recording: %s", message.c_str());
	}

	void run(Options options);
#ifdef __EMSCRIPTEN__
	std::uint64_t reportedFrames = 0, reportedAudio = 0;
	void pump()
	{
		if (char *value = browserRecordingStatus())
		{
			try
			{
				auto s = nlohmann::json::parse(value);
				std::lock_guard lock(mutex);
				published.state = static_cast<State>(s.value("state",int(State::Failed)));
				published.path = s.value("path",path); published.error = s.value("error","");
				published.encoder = s.value("encoder",""); published.fallbackReason = s.value("fallbackReason","");
				published.segment = s.value("segment",0u); published.width = s.value("width",0); published.height = s.value("height",0);
				published.sourceWidth = s.value("sourceWidth",0); published.sourceHeight = s.value("sourceHeight",0);
				published.droppedFrames = s.value("droppedFrames",std::uint64_t(0));
				published.droppedAudioSamples = s.value("droppedAudioSamples",std::uint64_t(0));
				published.outputs = s.value("outputs",std::vector<std::string>{});
				if (state != State::Failed && (state != State::Finalizing || published.state >= State::Complete)) state = published.state;
				error = published.error; finished = state == State::Complete || state == State::Failed;
			}
			catch (...) { fail("Invalid recording worker status"); }
			std::free(value);
		}
		for (;;)
		{
			Audio block;
			{
				std::lock_guard lock(audioMutex); if (!audioCount) break;
				block = audioBlocks[audioRead]; audioRead = (audioRead+1)%audioBlocks.size(); --audioCount; audioSamples -= block.count;
			}
			if (!browserRecordingAudio(block.samples.data(),int(block.count),double(block.time))) droppedAudio += block.count;
		}
		if (reportedFrames != droppedFrames || reportedAudio != droppedAudio)
		{
			reportedFrames = droppedFrames; reportedAudio = droppedAudio;
			browserRecordingDrops(double(reportedFrames),double(reportedAudio));
		}
	}
#endif

};
Recorder::Recorder() : impl(std::make_unique<Impl>()) {}
Recorder::~Recorder()
{
	shutdown();
}
std::int64_t timestamp()
{
	return now();
}
Recorder &recorder()
{
	static Recorder instance;
	return instance;
}
bool supported()
{
#if defined(__EMSCRIPTEN__)
	return browserRecordingAvailable();
#else
	return true;
#endif
}
bool available() { return supported(); }
bool Recorder::start(const std::string &requestedPath)
{
	if (active() || impl->state == State::Finalizing)
		return false;
	if (impl->worker.joinable())
	{
		if (!impl->finished)
			return false;
		impl->worker.join();
	}
	try
	{
		if (!supported())
			throw std::runtime_error("Recording worker is unavailable");
		if (options.fps < 1 || options.fps > 240 || options.crf < 0 || options.crf > 51 || !options.chapterTicks)
			throw std::runtime_error("Invalid recording options");
		auto path = std::filesystem::absolute(std::filesystem::u8path(requestedPath));
		if (path.extension() != ".mp4")
			throw std::runtime_error("Recording output must have a .mp4 extension");
#ifndef __EMSCRIPTEN__
		Detail::nativeSessionStorage().reserve(utf8(path));
#endif
		{
			std::lock_guard<std::mutex> lock(impl->mutex);
			impl->path = utf8(path);
			impl->error.clear();
			impl->published = {};
			impl->frames.clear();
			impl->events.clear();
		}
		{
			std::lock_guard<std::mutex> lock(impl->audioMutex);
			impl->audioRead = impl->audioWrite = impl->audioCount = impl->audioSamples = 0;
		}
		impl->matchSerial = impl->context.match ? 1 : 0;
		impl->context.match = impl->matchSerial;
		impl->epoch = 0;
		impl->stoppedAt = 0;
		impl->lastCapture = -1;
		impl->queuedFrames = 0;
		impl->droppedFrames = 0;
		impl->droppedAudio = 0;
		impl->captureFps = options.fps;
		impl->state = State::Starting;
		impl->finished = false;
#ifdef __EMSCRIPTEN__
		char *base = browserRecordingBase();
		browserRecordingStart(impl->path.c_str(),options.fps,options.crf,options.encoder == EncoderPreference::Software,options.chapterTicks,base,double(now()));
		std::free(base);
#else
		impl->worker = std::thread(
			[this, settings = options]
			{
				try
				{
					impl->run(settings);
				}
				catch (const std::exception &error)
				{
					impl->fail(error.what());
				}
				impl->finished = true;
			});
#endif
		return true;
	}
	catch (const std::exception &e)
	{
		impl->finished = true;
		impl->fail(e.what());
		return false;
	}
}
void Recorder::stop()
{
#ifdef __EMSCRIPTEN__
	impl->pump();
#endif
	{
		// Serialize stop time and failure publication; a failed encoder must never
		// be changed back into Finalizing by a simultaneous quit/shortcut request.
		std::lock_guard<std::mutex> lock(impl->mutex);
		auto expected = impl->state.load();
		if (expected != State::Starting && expected != State::Recording)
			return;
		impl->stoppedAt = now();
		while (!impl->state.compare_exchange_weak(expected, State::Finalizing))
		{
			if (expected != State::Starting && expected != State::Recording)
				return;
		}
	}
	impl->wake.notify_all();
#ifdef __EMSCRIPTEN__
	impl->pump();
	browserRecordingStop(double(impl->stoppedAt.load()));
#endif
}

void Recorder::abort(const std::string &error)
{
	if (!active())
		return;
	event("capture_error", error);
	impl->fail(error);
#ifdef __EMSCRIPTEN__
	browserRecordingFail(error.c_str());
#endif
}
void Recorder::shutdown()
{
	stop();
	if (impl->worker.joinable())
		impl->worker.join();
}
Status Recorder::status() const
{
#ifdef __EMSCRIPTEN__
	impl->pump();
#endif
	std::lock_guard<std::mutex> lock(impl->mutex);
	Status result = impl->published;
	result.state = impl->state.load(); result.path = result.path.empty() ? impl->path : result.path;
	result.error = impl->error; result.droppedFrames = std::max(result.droppedFrames,impl->droppedFrames.load()); result.droppedAudioSamples = std::max(result.droppedAudioSamples,impl->droppedAudio.load());
	return result;
}
bool Recorder::active() const
{
	return impl->active();
}
bool Recorder::wantsFrame() const
{
	if (!active())
		return false;
	auto epoch = impl->epoch.load();
	// One initial frame supplies the dimensions while the encoder initializes.
	if (epoch && impl->state == State::Starting) return false;
	if (epoch && (now() - epoch) * impl->captureFps / 1000000 <= impl->lastCapture)
		return false;
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (!lock) { ++impl->droppedFrames; return false; }
#ifdef __EMSCRIPTEN__
	if (!browserRecordingRoom()) { ++impl->droppedFrames; return false; }
#endif
	if (lock && impl->queuedFrames >= MaxQueuedFrames)
		++impl->droppedFrames;
	return lock && impl->queuedFrames < MaxQueuedFrames;
}
void Recorder::frame(const SDL_Surface &pixels, bool bottomUp)
{
	if (!active())
		return;
	const auto time = now();
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (!lock || impl->queuedFrames >= MaxQueuedFrames)
	{
		++impl->droppedFrames;
		return;
	}
	if (pixels.w <= 0 || pixels.h <= 0)
		return;
	unsigned layout = 0;
	if (pixels.format == SDL_PIXELFORMAT_BGRA32) layout = 1;
	else if (pixels.format == SDL_PIXELFORMAT_ARGB32) layout = 2;
	else if (pixels.format == SDL_PIXELFORMAT_ABGR32) layout = 3;
	else if (pixels.format == SDL_PIXELFORMAT_RGB24) layout = 4;
	else if (pixels.format == SDL_PIXELFORMAT_BGR24) layout = 5;
	else if (pixels.format != SDL_PIXELFORMAT_RGBA32)
	{
		lock.unlock(); abort("Unsupported recording capture pixel format"); return;
	}
	const auto bytesPerPixel = layout < 4 ? 4 : 3;
	Frame frame{time, pixels.w, pixels.h, {}, impl->context};
	frame.pixelLayout = layout; frame.bottomUp = bottomUp;
	frame.rgba.resize(std::size_t(frame.width) * frame.height * bytesPerPixel);
	for (int y = 0; y < frame.height; ++y)
		std::memcpy(frame.rgba.data() + std::size_t(y) * frame.width * bytesPerPixel,
					static_cast<const unsigned char *>(pixels.pixels) + y * pixels.pitch,
					std::size_t(frame.width) * bytesPerPixel);
	if (!impl->epoch)
		impl->epoch = time;
	impl->lastCapture = (time - impl->epoch) * impl->captureFps / 1000000;
#ifdef __EMSCRIPTEN__
	auto context = "{\"pixel_layout\":"+std::to_string(frame.pixelLayout)+",\"bottom_up\":"+(frame.bottomUp ? "true" : "false")+","+frame.context.fields(options.chapterTicks)+",\"speed\":"+std::to_string(frame.context.speed)+",\"paused\":"+(frame.context.paused ? "true" : "false")+"}";
	if (!browserRecordingFrame(frame.rgba.data(),frame.width,frame.height,double(frame.time),context.c_str())) ++impl->droppedFrames;
#else
	impl->frames.push_back(std::move(frame));
	++impl->queuedFrames;
	impl->wake.notify_all();
#endif
}
void Recorder::audio(const std::int16_t *samples, std::size_t count)
{
	audio(samples, count, timestamp());
}
void Recorder::audio(const std::int16_t *samples, std::size_t count, std::int64_t time)
{
	auto epoch = impl->epoch.load();
	if (!active() || !epoch)
		return;
	std::unique_lock<std::mutex> lock(impl->audioMutex, std::try_to_lock);
	if (!lock || !active())
	{
		impl->droppedAudio += count;
		return;
	}
	while (count)
	{
		auto n = std::min<std::size_t>(count, AudioBlockSamples);
		if (impl->audioCount == impl->audioBlocks.size() ||
			impl->audioSamples + n > AudioSampleBudget)
		{
			impl->droppedAudio += count;
			return;
		}
		auto &block = impl->audioBlocks[impl->audioWrite];
		block.time = time;
		block.count = n;
		std::memcpy(block.samples.data(), samples, n * sizeof(std::int16_t));
		impl->audioWrite = (impl->audioWrite + 1) % impl->audioBlocks.size();
		++impl->audioCount;
		impl->audioSamples += n;
		samples += n;
		count -= n;
		time += std::int64_t(n / 2) * 1000000 / 44100;
	}
}
void Recorder::screen(const std::string &id)
{
	impl->context.screen = id;
	impl->context.dialog.clear();
	if (id != "game_session" && id != "end_game")
	{
		impl->context.match = 0;
		impl->context.mode.clear();
		impl->context.map.clear();
		impl->context.team = -1;
	}
}
void Recorder::dialog(const std::string &id)
{
	impl->context.dialog = id;
}
void Recorder::beginMatch(const std::string &mode, const std::string &map, int team,
						  std::uint32_t tick)
{
	impl->context.match = ++impl->matchSerial;
	impl->context.mode = mode;
	impl->context.map = map;
	impl->context.team = team;
	impl->context.tick = tick;
	impl->context.paused = false;
	event("match_start", mode);
}
void Recorder::matchFrame(std::uint32_t tick, bool paused, int speed)
{
	if (paused != impl->context.paused)
		event("paused", paused ? "true" : "false");
	if (speed != impl->context.speed)
		event("speed", std::to_string(speed));
	impl->context.tick = tick;
	impl->context.paused = paused;
	impl->context.speed = speed;
	impl->context.screen = "game_session";
	impl->context.dialog.clear();
}
void Recorder::event(const std::string &kind, const std::string &value)
{
	if (!active())
		return;
#ifdef __EMSCRIPTEN__
	browserRecordingEvent(double(now()),kind.c_str(),value.c_str());
#else
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (lock && impl->events.size() < MaxQueuedEvents)
		impl->events.push_back({now(), kind, value});
#endif
}
bool toggle()
{
	auto &r = recorder();
	if (r.active())
	{
		r.stop();
		return true;
	}
	if (r.status().state == State::Finalizing || !supported())
		return false;
	auto root = Toolkit::getFileManager()->getDir(0);
	return r.start(utf8(std::filesystem::u8path(root) / "videoshots" /
						("capture-" + std::to_string(now()) + ".mp4")));
}
std::string controlLabel()
{
	auto state = recorder().status().state;
	if (state == State::Finalizing)
		return "[finalizing recording]";
	return recorder().active() ? "[stop recording]" : "[start recording]";
}

#ifndef __EMSCRIPTEN__
void Recorder::Impl::run(Options options)
{
	Detail::Session session(path, options, Detail::nativeSessionStorage(), [this](const Status &status)
	{
		std::lock_guard lock(mutex);
		published = status;
		if (state != State::Failed)
		{
			if (state != State::Finalizing || status.state == State::Complete || status.state == State::Failed)
				state = status.state;
			error = status.error;
		}
	});
	try
	{
		bool primed = false;
		for (;;)
		{
			std::deque<Frame> incoming;
			std::deque<Event> pending;
			{
				std::lock_guard lock(mutex);
				if (state == State::Failed) throw std::runtime_error(error);
				incoming.swap(frames); pending.swap(events);
			}
			const auto before = session.queuedFrames()+incoming.size();
			bool haveFrame = !incoming.empty();
			for (auto &frame : incoming) session.frame(std::move(frame));
			if (stoppedAt) session.stop(stoppedAt);
			if (!primed && haveFrame) { session.step(now()); primed = true; }
			for (const auto &e : pending) session.event(e.time,e.kind,e.value);
			// Audio may arrive faster than this worker can encode under pressure.
			// Bound each batch so a continuously nonempty audio queue cannot starve video.
			for (unsigned processed = 0; processed < 4; ++processed)
			{
				Audio block;
				{
					std::lock_guard lock(audioMutex);
					if (!audioCount) break;
					block = audioBlocks[audioRead]; audioRead = (audioRead+1)%audioBlocks.size();
					--audioCount; audioSamples -= block.count;
				}
				session.audio(block.samples.data(),block.count,block.time);
			}
			session.drops(droppedFrames,droppedAudio);
			const bool done = session.step(now());
			queuedFrames.fetch_sub(before-session.queuedFrames());
			if (done) break;
			std::unique_lock lock(mutex);
			wake.wait_for(lock,std::chrono::milliseconds(2));
		}
	}
	catch (const std::exception &e) { session.fail(e.what()); fail(e.what()); }
}
#endif
} // namespace GAGCore::Recording
