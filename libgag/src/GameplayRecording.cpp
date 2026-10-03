// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include "RecordingMetadata.h"
#include <RecordingProcess.h>
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

namespace GAGCore::Recording
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t MaxQueuedFrames = 3;
constexpr std::size_t MaxQueuedEvents = 1024;
constexpr std::size_t AudioBlockSamples = 4096;
constexpr std::size_t AudioSampleBudget = AudioSampleRate * AudioChannels * 2;
constexpr std::int64_t JitterToleranceFrames = AudioSampleRate / 50; // 20ms
constexpr std::int64_t VideoDelayUs = 50'000;
constexpr std::int64_t AudioCallbackAllowanceUs = 250'000;
std::int64_t now()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
		.count();
}
std::string utf8(const std::filesystem::path &path)
{
	auto value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}
using Detail::Context;
using Detail::ffescape;
using Detail::json;
struct Frame
{
	std::int64_t time;
	int width, height;
	std::vector<unsigned char> rgba;
	Context context;
};
struct Event
{
	std::int64_t time;
	std::string kind, value;
};
struct Chapter
{
	std::int64_t start, end;
	Context context;
	std::uint32_t lastTick;
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
	std::atomic<int> captureFps{60};
	std::mutex mutex, audioMutex;
	std::condition_variable wake;
	std::deque<Frame> frames;
	std::deque<Event> events;
	// Preallocated callback storage, with a separate sample budget of two seconds.
	std::array<Audio, 128> audioBlocks;
	std::size_t audioRead = 0, audioWrite = 0, audioCount = 0, audioSamples = 0;
	std::string path, error;
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
	void encodeAudio(const Options &options, const std::string &work, std::atomic<bool> &audioDone,
					 std::atomic<bool> &audioFailed, std::string &audioError);
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
#if defined(__EMSCRIPTEN__) || defined(GLOB2_MOBILE)
	return false;
#else
	return true;
#endif
}
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
			throw std::runtime_error("Video recording currently requires a desktop build");
		if (options.fps < 1 || options.fps > 240 || options.crf < 0 || options.crf > 51 ||
			!options.chapterTicks || options.width < 0 || options.height < 0 ||
			bool(options.width) != bool(options.height) || options.width > 16384 ||
			options.height > 16384)
			throw std::runtime_error("Invalid recording options");
		auto path = std::filesystem::absolute(std::filesystem::u8path(requestedPath));
		if (path.extension() != ".mp4")
			throw std::runtime_error("Recording output must have a .mp4 extension");
		std::filesystem::create_directories(path.parent_path());
		// Atomic directory reservation prevents two clients claiming the same output.
		if (std::filesystem::exists(path) ||
			std::filesystem::exists(std::filesystem::u8path(utf8(path) + ".json")) ||
			std::filesystem::exists(std::filesystem::u8path(utf8(path) + ".events.jsonl")) ||
			!std::filesystem::create_directory(std::filesystem::u8path(utf8(path) + ".recording")))
			throw std::runtime_error(
				"Recording output already exists or is reserved; choose a new name");
		{
			std::lock_guard<std::mutex> lock(impl->mutex);
			impl->path = utf8(path);
			impl->error.clear();
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
		impl->droppedFrames = 0;
		impl->droppedAudio = 0;
		impl->captureFps = options.fps;
		impl->state = State::Starting;
		impl->finished = false;
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
}

void Recorder::abort(const std::string &error)
{
	if (!active())
		return;
	event("capture_error", error);
	impl->fail(error);
}
void Recorder::shutdown()
{
	stop();
	if (impl->worker.joinable())
		impl->worker.join();
}
Status Recorder::status() const
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	return {impl->state.load(), impl->path, impl->error, impl->droppedFrames.load(),
			impl->droppedAudio.load()};
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
	if (epoch && (now() - epoch) * impl->captureFps / 1000000 <= impl->lastCapture)
		return false;
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (lock && impl->frames.size() >= MaxQueuedFrames)
		++impl->droppedFrames;
	return lock && impl->frames.size() < MaxQueuedFrames;
}
void Recorder::frame(const SDL_Surface &pixels)
{
	if (!active())
		return;
	const auto time = now();
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (!lock || impl->frames.size() >= MaxQueuedFrames)
	{
		++impl->droppedFrames;
		return;
	}
	if (pixels.w <= 0 || pixels.h <= 0)
		return;
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(nullptr,
																		  SDL_DestroySurface);
	const SDL_Surface *source = &pixels;
	if (pixels.format != SDL_PIXELFORMAT_RGBA32)
	{
		converted.reset(
			SDL_ConvertSurface(const_cast<SDL_Surface *>(&pixels), SDL_PIXELFORMAT_RGBA32));
		if (!converted)
		{
			lock.unlock();
			abort(SDL_GetError());
			return;
		}
		source = converted.get();
	}
	Frame frame{time, source->w, source->h, {}, impl->context};
	frame.rgba.resize(std::size_t(frame.width) * frame.height * 4);
	for (int y = 0; y < frame.height; ++y)
		std::memcpy(frame.rgba.data() + std::size_t(y) * frame.width * 4,
					static_cast<const unsigned char *>(source->pixels) + y * source->pitch,
					std::size_t(frame.width) * 4);
	if (!impl->epoch)
		impl->epoch = time;
	impl->lastCapture = (time - impl->epoch) * impl->captureFps / 1000000;
	impl->frames.push_back(std::move(frame));
	impl->wake.notify_all();
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
	std::unique_lock<std::mutex> lock(impl->mutex, std::try_to_lock);
	if (lock && impl->events.size() < MaxQueuedEvents)
		impl->events.push_back({now(), kind, value});
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

void Recorder::Impl::encodeAudio(const Options &options, const std::string &work,
								 std::atomic<bool> &audioDone, std::atomic<bool> &audioFailed,
								 std::string &audioError)
{
	try
	{
		// delay_moov preserves AAC priming/edit-list information. empty_moov
		// would shift decoded samples by one 1024-frame AAC block on remux.
		Process audio;
		audio.launch({options.ffmpeg,
					  "-hide_banner",
					  "-loglevel",
					  "warning",
					  "-nostdin",
					  "-n",
					  "-f",
					  "s16le",
					  "-ar",
					  "44100",
					  "-ac",
					  "2",
					  "-i",
					  "pipe:0",
					  "-vn",
					  "-c:a",
					  "aac",
					  "-b:a",
					  "192k",
					  "-movflags",
					  "+frag_keyframe+delay_moov+default_base_moof",
					  "-frag_duration",
					  "1000000",
					  work + "audio.mp4"},
					 work + "audio.log", true);
		std::uint64_t written = 0;
		std::array<std::int16_t, AudioBlockSamples> silence{};
		auto pad = [&](std::uint64_t target)
		{
			while (written < target)
			{
				auto n = std::min<std::uint64_t>(target - written, silence.size() / 2);
				audio.write(silence.data(), n * 4);
				written += n;
			}
		};
		for (;;)
		{
			Audio block;
			bool available = false;
			{
				std::lock_guard<std::mutex> lock(audioMutex);
				if (audioCount)
				{
					block = audioBlocks[audioRead];
					audioRead = (audioRead + 1) % audioBlocks.size();
					--audioCount;
					audioSamples -= block.count;
					available = true;
				}
			}
			if (available)
			{
				auto start =
					std::max<std::int64_t>(0, (block.time - epoch) * AudioSampleRate / 1000000);
				// Preserve continuous PCM through normal scheduling jitter; correct
				// gaps or drift once they exceed 20ms on the common timeline.
				if (std::abs(start - std::int64_t(written)) < JitterToleranceFrames)
					start = std::int64_t(written);
				auto end = std::uint64_t(start) + block.count / 2;
				// Correct callback/device clock drift against the common recording clock.
				pad(std::uint64_t(start));
				if (end > written)
				{
					auto skip = written - std::uint64_t(start);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					for (std::size_t i = 0; i < block.count; ++i)
						block.samples[i] = SDL_Swap16(block.samples[i]);
#endif
					audio.write(block.samples.data() + skip * 2, (end - written) * 4);
					written = end;
				}
			}
			else
			{
				auto end = stoppedAt.load();
				if (audioDone)
				{
					pad(std::uint64_t(std::max<std::int64_t>(0, end - epoch)) * AudioSampleRate /
						1000000);
					break;
				}
				// Leave 250ms for late callbacks; muted/stopped devices become silence.
				auto target =
					(now() - epoch - AudioCallbackAllowanceUs) * AudioSampleRate / 1000000;
				if (target > 0)
					pad(std::uint64_t(target));
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
		if (audio.finish())
			throw std::runtime_error("Audio encoder failed; inspect audio.log");
	}
	catch (const std::exception &e)
	{
		audioError = e.what();
		audioFailed = true;
	}
}

void Recorder::Impl::run(Options options)
{
	const std::string work = path + ".recording/";
	std::ofstream journal(std::filesystem::u8path(work + "events.jsonl"), std::ios::binary);
	std::vector<Chapter> chapters;
	std::uint64_t videoFrames = 0;
	int width = 0, height = 0;
	auto writeManifest = [&](bool complete, std::int64_t duration)
	{
		std::string recordedError;
		{
			std::lock_guard<std::mutex> lock(mutex);
			recordedError = error;
		}
		std::ofstream manifest(std::filesystem::u8path(work + "manifest.json"), std::ios::binary);
		manifest << "{\"version\":1,\"video\":"
				 << json(utf8(std::filesystem::u8path(path).filename()))
				 << ",\"complete\":" << (complete ? "true" : "false")
				 << ",\"error\":" << json(recordedError) << ",\"duration_us\":" << duration
				 << ",\"fps\":" << options.fps << ",\"width\":" << width << ",\"height\":" << height
				 << ",\"crf\":" << options.crf
				 << ",\"video_codec\":\"h264\",\"audio_codec\":\"aac\",\"dropped_frames\":"
				 << droppedFrames << ",\"dropped_audio_samples\":" << droppedAudio
				 << ",\"chapters\":[";
		for (std::size_t i = 0; i < chapters.size(); ++i)
		{
			auto &chapter = chapters[i];
			if (i)
				manifest << ',';
			manifest << "{\"id\":" << i + 1
					 << ",\"title\":" << json(chapter.context.title(options.chapterTicks))
					 << ",\"start_us\":" << chapter.start << ",\"end_us\":" << chapter.end << ','
					 << chapter.context.fields(options.chapterTicks);
			if (chapter.context.match)
				manifest << ",\"start_tick\":" << chapter.context.tick
						 << ",\"last_tick\":" << chapter.lastTick;
			manifest << '}';
		}
		manifest << "]}\n";
		manifest.flush();
		if (!manifest)
			throw std::runtime_error("Cannot write recording manifest");
	};
	std::atomic<bool> audioDone{false}, audioFailed{false};
	std::string audioError;
	std::thread audioWorker;
	try
	{
		if (!journal)
			throw std::runtime_error("Cannot write recording event journal");
		Process probe;
		probe.launch({options.ffmpeg, "-hide_banner", "-encoders"}, work + "preflight.log", false);
		if (probe.finish(5))
			throw std::runtime_error("FFmpeg preflight failed; inspect preflight.log");
		std::ifstream input(std::filesystem::u8path(work + "preflight.log"));
		std::string encoders((std::istreambuf_iterator<char>(input)), {});
		if (encoders.find("libx264 ") == std::string::npos ||
			encoders.find(" aac ") == std::string::npos)
			throw std::runtime_error("FFmpeg must provide libx264 and AAC encoders");
		Frame latest;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [&] { return !frames.empty() || !active(); });
			if (frames.empty())
				throw std::runtime_error("Recording stopped before a frame was captured");
			latest = std::move(frames.front());
			frames.pop_front();
		}
		width = options.width ? options.width : latest.width;
		height = options.height ? options.height : latest.height;
		width += width % 2;
		height += height % 2;
		Process video;
		video.launch({options.ffmpeg,
					  "-hide_banner",
					  "-loglevel",
					  "warning",
					  "-nostdin",
					  "-n",
					  "-f",
					  "rawvideo",
					  "-pixel_format",
					  "rgba",
					  "-video_size",
					  std::to_string(width) + "x" + std::to_string(height),
					  "-framerate",
					  std::to_string(options.fps),
					  "-i",
					  "pipe:0",
					  "-an",
					  "-c:v",
					  "libx264",
					  "-preset",
					  "veryfast",
					  "-crf",
					  std::to_string(options.crf),
					  "-pix_fmt",
					  "yuv420p",
					  "-g",
					  std::to_string(options.fps * 2),
					  "-movflags",
					  "+frag_keyframe+empty_moov+default_base_moof",
					  work + "video.mp4"},
					 work + "video.log", true);
		audioWorker =
			std::thread([&] { encodeAudio(options, work, audioDone, audioFailed, audioError); });
		auto canvas = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
			SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		if (!canvas)
			throw std::runtime_error(SDL_GetError());
		std::vector<unsigned char> rgba(std::size_t(width) * height * 4);
		auto prepare = [&]
		{
			SDL_FillSurfaceRect(canvas.get(), nullptr,
								SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
			auto source = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
				SDL_CreateSurfaceFrom(latest.width, latest.height, SDL_PIXELFORMAT_RGBA32,
									  latest.rgba.data(), latest.width * 4),
				SDL_DestroySurface);
			if (!source)
				throw std::runtime_error(SDL_GetError());
			double scale = std::min(double(width) / latest.width, double(height) / latest.height);
			SDL_Rect target{0, 0, std::max(1, int(latest.width * scale)),
							std::max(1, int(latest.height * scale))};
			target.x = (width - target.w) / 2;
			target.y = (height - target.h) / 2;
			SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_NONE);
			if (!SDL_BlitSurfaceScaled(source.get(), nullptr, canvas.get(), &target,
									   SDL_SCALEMODE_LINEAR))
				throw std::runtime_error(SDL_GetError());
			for (int y = 0; y < height; ++y)
				std::memcpy(rgba.data() + std::size_t(y) * width * 4,
							static_cast<unsigned char *>(canvas->pixels) + y * canvas->pitch,
							std::size_t(width) * 4);
		};
		prepare();
		auto lastWidth = latest.width, lastHeight = latest.height;
		auto requireHealthyCapture = [&]
		{
			if (state != State::Failed)
				return;
			std::lock_guard<std::mutex> lock(mutex);
			throw std::runtime_error(error);
		};
		auto journalEventsThrough = [&](std::int64_t relative)
		{
			std::deque<Event> pending;
			{
				std::lock_guard<std::mutex> lock(mutex);
				while (!events.empty() && events.front().time <= epoch + relative)
				{
					pending.push_back(std::move(events.front()));
					events.pop_front();
				}
			}
			for (const auto &event : pending)
				journal << "{\"time_us\":" << std::max<std::int64_t>(0, event.time - epoch)
						<< ",\"kind\":" << json(event.kind) << ",\"value\":" << json(event.value)
						<< "}\n";
			if (!pending.empty())
			{
				journal.flush();
				if (!journal)
					throw std::runtime_error("Cannot write recording journal");
			}
		};
		std::uint64_t reportedFrames = 0, reportedAudio = 0;
		for (;;)
		{
			requireHealthyCapture();
			if (audioFailed)
				throw std::runtime_error("Audio encoder stopped; inspect audio.log");
			auto relative = std::int64_t(videoFrames) * 1000000 / options.fps;
			auto end = stoppedAt.load();
			if (!active() && end && relative >= std::max<std::int64_t>(1, end - epoch))
				break;
			// A small presentation delay allows submitted frames to reach their correct output slot.
			if (active() && now() < epoch + relative + VideoDelayUs)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			bool changed = false;
			{
				std::lock_guard<std::mutex> lock(mutex);
				while (!frames.empty() && frames.front().time <= epoch + relative)
				{
					latest = std::move(frames.front());
					frames.pop_front();
					changed = true;
				}
			}
			journalEventsThrough(relative);
			const auto frameDrops = droppedFrames.load(), audioDrops = droppedAudio.load();
			if (frameDrops != reportedFrames || audioDrops != reportedAudio)
			{
				journal << "{\"time_us\":" << relative
						<< ",\"kind\":\"capture_gap\",\"dropped_frames\":"
						<< frameDrops - reportedFrames
						<< ",\"dropped_audio_samples\":" << audioDrops - reportedAudio << "}\n";
				reportedFrames = frameDrops;
				reportedAudio = audioDrops;
				journal.flush();
				if (!journal)
					throw std::runtime_error("Cannot write recording journal");
			}
			if (changed)
			{
				prepare();
				if (lastWidth != latest.width || lastHeight != latest.height)
					journal << "{\"time_us\":" << relative
							<< ",\"kind\":\"resize\",\"width\":" << latest.width
							<< ",\"height\":" << latest.height << "}\n";
				lastWidth = latest.width;
				lastHeight = latest.height;
			}
			if (chapters.empty() || chapters.back().context.key(options.chapterTicks) !=
										latest.context.key(options.chapterTicks))
			{
				if (!chapters.empty())
					chapters.back().end = relative;
				chapters.push_back({relative, relative, latest.context, latest.context.tick});
				journal << "{\"time_us\":" << relative
						<< ",\"kind\":\"chapter\",\"id\":" << chapters.size() << ','
						<< latest.context.fields(options.chapterTicks) << "}\n";
				journal.flush();
				if (!journal)
					throw std::runtime_error("Cannot write recording journal");
			}
			chapters.back().lastTick = latest.context.tick;
			video.write(rgba.data(), rgba.size());
			++videoFrames;
			State expected = State::Starting;
			state.compare_exchange_strong(expected, State::Recording);
		}
		auto duration = std::int64_t(videoFrames) * 1000000 / options.fps;
		if (!chapters.empty())
			chapters.back().end = duration;
		if (!stoppedAt)
			stoppedAt = epoch + duration;
		audioDone = true;
		audioWorker.join();
		if (audioFailed)
			throw std::runtime_error(audioError);
		if (video.finish())
			throw std::runtime_error("Video encoder failed; inspect video.log");
		requireHealthyCapture();
		journalEventsThrough(duration);
		writeManifest(false, duration);
		std::ofstream metadata(std::filesystem::u8path(work + "chapters.ffmetadata"),
							   std::ios::binary);
		metadata << ";FFMETADATA1\n";
		for (const auto &chapter : chapters)
			metadata << "[CHAPTER]\nTIMEBASE=1/1000000\nSTART=" << chapter.start
					 << "\nEND=" << chapter.end
					 << "\ntitle=" << ffescape(chapter.context.title(options.chapterTicks)) << '\n';
		metadata.close();
		if (!metadata)
			throw std::runtime_error("Cannot write embedded chapter metadata");
		Process mux;
		mux.launch({options.ffmpeg,
					"-hide_banner",
					"-loglevel",
					"warning",
					"-nostdin",
					"-n",
					"-i",
					work + "video.mp4",
					"-i",
					work + "audio.mp4",
					"-f",
					"ffmetadata",
					"-i",
					work + "chapters.ffmetadata",
					"-map",
					"0:v:0",
					"-map",
					"1:a:0",
					"-map_chapters",
					"2",
					"-c",
					"copy",
					"-t",
					std::to_string(double(duration) / 1000000),
					"-movflags",
					"+faststart",
					work + "final.mp4"},
				   work + "finalize.log", false);
		if (mux.finish(120))
			throw std::runtime_error(
				"Recording finalization failed; compressed tracks and metadata were retained");
		writeManifest(true, duration);
		journal.close();
		if (!journal)
			throw std::runtime_error("Cannot close recording journal");
		Detail::publishCompletedRecording(std::filesystem::u8path(work),
										  std::filesystem::u8path(path));
		std::error_code cleanupError;
		std::filesystem::remove_all(std::filesystem::u8path(path + ".recording"), cleanupError);
		if (cleanupError)
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Recording saved; cleanup failed: %s",
						cleanupError.message().c_str());
		state = State::Complete;
		SDL_Log("Recording saved: %s", path.c_str());
	}
	catch (const std::exception &e)
	{
		if (!stoppedAt)
			stoppedAt = now();
		audioDone = true;
		if (audioWorker.joinable())
			audioWorker.join();
		fail(e.what());
		if (!chapters.empty())
			chapters.back().end = std::int64_t(videoFrames) * 1000000 / options.fps;
		try
		{
			writeManifest(false, std::int64_t(videoFrames) * 1000000 / options.fps);
		}
		catch (...)
		{
		}
	}
}
} // namespace GAGCore::Recording
