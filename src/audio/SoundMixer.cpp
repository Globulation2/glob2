// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SoundMixer.h"
#include <random>
#include "MusicProducer.h"
#include "MusicBuffer.h"
#include "Order.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <GameplayRecording.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cctype>
#include <functional>
#include <optional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ThreadSupport.h>
#ifndef __EMSCRIPTEN__
#ifndef GLOB2_NO_VOICE
#include <speex/speex.h>
#endif
#else
#include "BrowserMusic.h"
#endif
using namespace GAGCore;
namespace
{
std::string resolve(const std::string &path)
{
	if (std::filesystem::path(path).is_absolute())
		return path;
	auto *files = Toolkit::getFileManager();
	for (unsigned i = 0; i < files->getDirCount(); ++i)
	{
		auto candidate = files->getDir(i) + "/" + path;
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate, error))
			return candidate;
	}
	return path;
}
std::array<std::string, 3> resolved(const std::array<std::string, 3> &paths)
{
	return {resolve(paths[0]), resolve(paths[1]), resolve(paths[2])};
}
} // namespace
#ifndef __EMSCRIPTEN__
struct SoundMixer::Impl
{
	Music::Buffer buffer;
	std::atomic<unsigned> music{255}, voice{255}, maxRenderUs{0}, maxCallbackUs{0};
	SDL_AudioStream *stream = nullptr;
	std::mutex mutex;
	std::condition_variable wake;
	std::function<void(Music::Producer &)> job;
	struct Selection
	{
		unsigned first;
		bool second;
		unsigned id, time;
	};
	std::optional<Selection> selection;
	unsigned nextCommand = 0;
	struct PreviewCommand
	{
		double value;
		unsigned order;
	};
	std::array<std::optional<PreviewCommand>, 7> controls;
	unsigned nextControl = 0;
	bool stopping = false, stopRequested = false;
	std::atomic<bool> running{false};
	std::thread worker;
	std::unique_ptr<Music::Producer> offline;
	Music::Snapshot snapshot;
	struct Voice
	{
		// One maximum-size voice packet plus its startup padding must fit.
		Music::Ring<float, 32768> pcm;
		float first = 0, second = 0, fraction = 0;
	};
	std::array<Voice, 32> voices;
	void *decoder = nullptr;
	Impl()
	{
		try
		{
			worker = ThreadSupport::launch(
				[this]
				{
					if (SDL_GetHintBoolean("GLOB2_AUDIO_THREAD_PRIORITY", true))
						SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
					Music::Producer producer;
					bool filling = true, deviceStarted = false;
					Selection lastSelection{};
					for (;;)
					{
						{
							std::unique_lock lock(mutex);
							wake.wait_for(lock, std::chrono::milliseconds(5),
										  [&] {
											  return stopping || bool(job) || selection ||
													 stopRequested || hasControls();
										  });
							if (stopping)
								break;
							auto task = std::move(job);
							job = {};
							auto select = selection;
							selection.reset();
							auto commands = controls;
							controls = {};
							bool stop = stopRequested;
							stopRequested = false;
							lock.unlock();
							if (select)
							{
								producer.select(select->first, select->second, running);
								lastSelection = *select;
								if (!running)
									lastSelection.id = 0;
							}
							if (stop)
								producer.stop();
							// Coalesce each control but retain order between different controls
							// (for example, Reset followed by Mood must leave that mood active).
							std::array<unsigned, 7> order{0, 1, 2, 3, 4, 5, 6};
							std::sort(order.begin(), order.end(),
									  [&](unsigned a, unsigned b)
									  {
										  if (!commands[a] || !commands[b])
											  return bool(commands[a]) > bool(commands[b]);
										  return int(commands[a]->order - commands[b]->order) < 0;
									  });
							for (unsigned i : order)
								if (commands[i])
									controlPreview(producer, Music::Control(i), commands[i]->value);
							if (task)
								task(producer);
						}
						if (!running || (producer.actTrack < 0 && !producer.preview))
							continue;
						if (buffer.needsPrefill.load(std::memory_order_acquire) ||
							buffer.queue.size() <= Music::LowBlocks)
							filling = true;
						if (!filling)
							continue;
						// One block per iteration keeps controls responsive; no wait while filling.
						while (buffer.queue.size() < Music::TargetBlocks)
						{
							{
								std::lock_guard lock(mutex);
								if (stopping || job || selection || stopRequested || hasControls())
									break;
							}
							auto *block = buffer.queue.writable();
							if (!block)
								break;
							const auto start = std::chrono::steady_clock::now();
							block->generation = buffer.generation.load();
							producer.render(block->pcm.data(), Music::Chunk);
							block->state = producer.snapshot();
							const bool audible =
								producer.actTrack == int(lastSelection.first) ||
								(producer.mode == Music::Producer::MODE_EARLY_CHANGE &&
								 producer.nextTrack == int(lastSelection.first));
							block->commandId = audible ? lastSelection.id : 0;
							block->commandAt = lastSelection.time;
							auto us =
								unsigned(std::chrono::duration_cast<std::chrono::microseconds>(
											 std::chrono::steady_clock::now() - start)
											 .count());
							maxRenderUs.store(std::max(us, maxRenderUs.load()));
							buffer.queue.commit();
						}
						filling = buffer.queue.size() < Music::TargetBlocks;
						if (!deviceStarted && !filling)
							deviceStarted = SDL_ResumeAudioStreamDevice(stream);
					}
				});
		}
		catch (const std::exception &error)
		{
			std::cerr << "Audio worker unavailable: " << error.what() << '\n';
			offline = std::make_unique<Music::Producer>();
		}
	}
	// Called with mutex held: commands must interrupt a refill as well as wake idle work.
	bool hasControls() const
	{
		return std::any_of(controls.begin(), controls.end(), [](const auto &v) { return bool(v); });
	}
	void controlPreview(Music::Producer &producer, Music::Control command, double value)
	{
		if (!producer.preview)
			return;
		if (command == Music::Control::Play)
		{
			if (value && !producer.preview->playing)
			{
				producer.control(command, value);
				invalidate(); // Only initial play replaces the preview's prepared silence.
			}
			buffer.paused.store(!value, std::memory_order_release);
			return;
		}
		if (command == Music::Control::Reset)
		{
			Music::Snapshot consumed;
			if (buffer.snapshot.read(consumed, buffer.generation.load()) && consumed.preview)
				producer.control(Music::Control::Seek, consumed.position);
		}
		producer.control(command, value);
		if (command == Music::Control::Seek || command == Music::Control::Reset)
			invalidate();
	}
	template <class F> auto invoke(F fn)
	{
		if (offline)
			return fn(*offline);
		using R = decltype(fn(std::declval<Music::Producer &>()));
		auto task = std::make_shared<std::packaged_task<R(Music::Producer &)>>(std::move(fn));
		auto result = task->get_future();
		{
			std::lock_guard lock(mutex);
			job = [task](Music::Producer &p) { (*task)(p); };
		}
		wake.notify_one();
		return result.get();
	}
	void invalidate() { buffer.generation.fetch_add(1, std::memory_order_release); }
	void open()
	{
		if (stream || offline)
			return;
		Recording::recorder();
		const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, Music::Rate};
		stream =
			SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
		if (!stream)
		{
			std::cerr << "Audio: " << SDL_GetError() << '\n';
			return;
		}
#ifndef GLOB2_NO_VOICE
		decoder = speex_decoder_init(speex_lib_get_mode(SPEEX_MODEID_NB));
#endif
		running = true;
		wake.notify_one();
	}
	~Impl()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		wake.notify_one();
		if (worker.joinable())
			worker.join();
		if (stream)
			SDL_DestroyAudioStream(stream); // callback lifetime barrier
#ifndef GLOB2_NO_VOICE
		if (decoder)
			speex_decoder_destroy(decoder);
#endif
	}
	static void SDLCALL callback(void *ptr, SDL_AudioStream *stream, int additional, int)
	{
		auto &self = *static_cast<Impl *>(ptr);
		struct CallbackTime
		{
			std::atomic<unsigned> &maximum;
			Uint64 start = SDL_GetTicksNS();
			~CallbackTime()
			{
				auto us = unsigned((SDL_GetTicksNS() - start) / 1000);
				maximum.store(std::max(us, maximum.load()), std::memory_order_relaxed);
			}
		} timer{self.maxCallbackUs};
		std::array<std::int16_t, Music::Chunk * 2> pcm;
		auto time =
			Recording::timestamp() + std::int64_t(std::max(0, SDL_GetAudioStreamQueued(stream))) *
										 1000000 / (Music::Rate * 4);
		while (additional > 0)
		{
			unsigned frames = std::min(unsigned((additional + 3) / 4), Music::Chunk);
			self.buffer.consume(pcm.data(), frames, unsigned(SDL_GetTicks()));
			const unsigned music = self.music.load(std::memory_order_relaxed),
						   voice = self.voice.load(std::memory_order_relaxed);
			for (unsigned i = 0; i < frames * 2; ++i)
			{
				int value = music == 255 ? pcm[i] : (int(pcm[i]) * int(music)) >> 8;
				float mixed = 0;
				bool speaking = false;
				for (auto &v : self.voices)
				{
					if (!v.pcm.front())
					{
						v.first = v.second = v.fraction = 0;
						continue;
					}
					speaking = true;
					mixed += (1 - v.fraction) * v.first + v.fraction * v.second;
					v.fraction += 1.0f / 12.0f;
					if (v.fraction > 1)
					{
						v.fraction -= 1;
						v.first = v.second;
						v.pcm.pop();
						if (auto *next = v.pcm.front())
							v.second = *next;
					}
				}
				if (speaking)
					value =
						(value + int(3 * std::clamp(mixed, -32767.0f, 32767.0f) * voice / 256)) / 4;
				pcm[i] = std::int16_t(std::clamp(value, -32768, 32767));
			}
			Recording::recorder().audio(pcm.data(), frames * 2, time);
			if (!SDL_PutAudioStreamData(stream, pcm.data(), int(frames * 4)))
				return;
			time += std::int64_t(frames) * 1000000 / Music::Rate;
			additional -= int(frames * 4);
		}
	}
};
#else
struct SoundMixer::Impl
{
	BrowserMusic browser;
};
#endif
SoundMixer::SoundMixer(unsigned music, unsigned voice, bool mute) : impl(std::make_unique<Impl>())
{
	musicRandom.initializeOwner(std::random_device{}(), unsigned(RandomDomain::Music));
	setVolume(music, voice, mute);
}
SoundMixer::~SoundMixer() = default;
int SoundMixer::loadTrack(std::string name, int index)
{
	name = resolve(name);
#ifndef __EMSCRIPTEN__
	return impl->invoke(
		[&](Music::Producer &p)
		{
			auto result = p.load(name, index);
			if (result >= 0 && (p.actTrack == result || p.nextTrack == result))
				impl->invalidate();
			return result;
		});
#else
	return impl->browser.load(name, index);
#endif
}
int SoundMixer::loadTrack(std::string name, MusicTrack index)
{
	return loadTrack(std::move(name), int(index));
}
bool SoundMixer::loadTracks(const std::vector<std::pair<std::string, MusicTrack>> &requests)
{
	bool ok = true;
	for (auto &[name, index] : requests)
		ok = (loadTrack(name, index) >= 0) && ok;
	return ok;
}
void SoundMixer::setNextTrack(unsigned index, bool early)
{
#ifndef __EMSCRIPTEN__
	if (impl->offline)
	{
		impl->offline->select(index, early, false);
		return;
	}
	{
		std::lock_guard lock(impl->mutex);
		impl->selection =
			Impl::Selection{index, early, ++impl->nextCommand, unsigned(SDL_GetTicks())};
		impl->stopRequested = false;
	}
	impl->wake.notify_one();
#else
	impl->browser.select(index, early);
#endif
}
void SoundMixer::setNextTrack(MusicTrack index, bool early)
{
	setNextTrack(unsigned(index), early);
}
void SoundMixer::stopMusic()
{
#ifndef __EMSCRIPTEN__
	{
		std::lock_guard lock(impl->mutex);
		impl->stopRequested = true;
		impl->selection.reset();
	}
	impl->wake.notify_one();
#else
	impl->browser.command("stop");
#endif
}
void SoundMixer::setVolume(unsigned music, unsigned voice, bool mute)
{
#ifndef __EMSCRIPTEN__
	impl->music = mute ? 0 : std::min(255u, music);
	impl->voice = mute ? 0 : std::min(255u, voice);
	if (!mute)
	{
		bool closed = !impl->stream;
		impl->open();
		if (closed && impl->stream)
			impl->invoke(
				[](Music::Producer &p)
				{
					if (p.actTrack >= 0)
						p.select(p.actTrack, false);
				});
	}
#else
	impl->browser.volume(music, voice, mute);
#endif
}
bool SoundMixer::enabled() const
{
#ifndef __EMSCRIPTEN__
	return impl->stream != nullptr;
#else
	return impl->browser.enabled();
#endif
}
bool SoundMixer::selectMusicSet(const std::string &preference)
{
	auto candidates = getMusicSets();
	if (preference.empty())
		for (size_t i = candidates.size(); i > 1; --i)
			std::swap(candidates[i - 1], candidates[size_t(musicRandom.nextU32()) % i]);
	else if (std::find(candidates.begin(), candidates.end(), preference) != candidates.end())
		candidates = {preference};
	else
		return false;
	for (const auto &name : candidates)
	{
		std::array<std::string, 3> paths;
		for (unsigned i = 0; i < 3; ++i)
			paths[i] = resolve("data/zik/" + name + "/a" + std::to_string(i + 1) + ".opus");
#ifndef __EMSCRIPTEN__
		bool ok = impl->invoke(
			[&](Music::Producer &p)
			{
				if (activeMusicSet == name && p.tracks.size() >= 5)
					return true;
				bool result = p.replace(paths);
				if (result)
					impl->invalidate();
				return result;
			});
#else
		bool ok = impl->browser.replace(paths);
#endif
		if (ok)
		{
			activeMusicSet = name;
			return true;
		}
	}
	return false;
}
unsigned SoundMixer::openPreview(const std::array<std::string, 3> &paths)
{
	auto full = resolved(paths);
#ifndef __EMSCRIPTEN__
	bool ok = impl->invoke(
		[&](Music::Producer &p)
		{
			bool ok = p.openPreview(full);
			if (ok)
			{
				impl->buffer.paused.store(true, std::memory_order_release);
				impl->invalidate();
				impl->snapshot = p.snapshot();
			}
			return ok;
		});
#else
	bool ok = impl->browser.preview(full);
#endif
	if (!ok)
		return 0;
	if (++nextPreviewSession == 0)
		++nextPreviewSession;
	return previewSession = nextPreviewSession;
}
void SoundMixer::closePreview(unsigned session)
{
	if (!session || session != previewSession)
		return;
	previewSession = 0;
#ifndef __EMSCRIPTEN__
	impl->invoke(
		[&](Music::Producer &p)
		{
			p.preview.reset();
			impl->buffer.paused.store(false, std::memory_order_release);
			impl->invalidate();
		});
#else
	impl->browser.command("closePreview");
#endif
}
void SoundMixer::previewControl(unsigned session, Music::Control command, double value)
{
	if (!session || session != previewSession || unsigned(command) >= 7)
		return;
#ifndef __EMSCRIPTEN__
	{
		std::lock_guard lock(impl->mutex);
		impl->controls[unsigned(command)] = Impl::PreviewCommand{value, ++impl->nextControl};
	}
	impl->wake.notify_one();
#else
	impl->browser.control(command, value);
#endif
}
Music::Snapshot SoundMixer::playbackSnapshot()
{
#ifndef __EMSCRIPTEN__
	impl->buffer.snapshot.read(impl->snapshot, impl->buffer.generation.load());
	if (!impl->running)
		impl->snapshot = impl->invoke([](Music::Producer &p) { return p.snapshot(); });
	if (impl->snapshot.preview && impl->buffer.paused.load(std::memory_order_acquire))
		impl->snapshot.playing = false;
	return impl->snapshot;
#else
	return impl->browser.snapshot();
#endif
}
Music::Diagnostics SoundMixer::diagnostics() const
{
#ifndef __EMSCRIPTEN__
	return {impl->buffer.queue.size() * Music::Chunk,
			impl->buffer.starvationFrames.load(),
			impl->buffer.underruns.load(),
			impl->buffer.consumedFrames.load(),
			impl->maxRenderUs.load(),
			impl->maxCallbackUs.load(),
			impl->buffer.maxCommandLatencyUs.load()};
#else
	return impl->browser.diagnostics();
#endif
}
bool SoundMixer::isPlayerTransmittingVoice(int player)
{
#ifndef __EMSCRIPTEN__
	return player >= 0 && player < 32 && impl->voices[player].pcm.size() > 0;
#else
	return false;
#endif
}
void SoundMixer::addVoiceData(std::shared_ptr<OrderVoiceData> order)
{
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_NO_VOICE)
	if (!impl->decoder || order->sender < 0 || order->sender >= 32)
		return;
	auto &pcm = impl->voices[order->sender].pcm;
	SpeexBits bits;
	speex_bits_init(&bits);
	speex_bits_read_from(&bits, (char *)order->getFramesData(), order->framesDataLength);
	if (!pcm.size())
		for (unsigned i = 0; i < 2000; ++i)
			if (auto *slot = pcm.writable())
			{
				*slot = 0;
				pcm.commit();
			}
	for (unsigned i = 0; i < order->frameCount; ++i)
	{
		std::array<float, 160> samples;
		if (speex_decode(impl->decoder, &bits, samples.data()) < 0)
			break;
		for (auto sample : samples)
			if (auto *slot = pcm.writable())
			{
				*slot = sample;
				pcm.commit();
			}
	}
	speex_bits_destroy(&bits);
#endif
}
std::vector<std::string> SoundMixer::getMusicSets()
{
	auto *files = Toolkit::getFileManager();
	files->initDirectoryListing("data/zik/", "", true);
	std::vector<std::string> result;
	std::string name;
	while (!(name = files->getNextDirectoryEntry()).empty())
	{
		if (name == "." || name == ".." || name.find_first_of("/\\\r\n=") != std::string::npos)
			continue;
		const std::string directory = "data/zik/" + name;
		if (!files->isDir(directory))
			continue;
		bool complete = true;
		for (int i = 1; i <= 3; ++i)
		{
			FILE *file = files->openFP(directory + "/a" + std::to_string(i) + ".opus");
			if (file)
				fclose(file);
			else
				complete = false;
		}
		if (complete)
			result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

std::string SoundMixer::musicSetLabel(const std::string &name)
{
	if (name.rfind("community-", 0) == 0)
	{
		Music::Producer metadata;
		if (metadata.load(resolve("data/zik/" + name + "/a1.opus"), 0) >= 0)
		{
			const char *album = opus_tags_query(op_tags(metadata.tracks[0], 0), "ALBUM", 0);
			std::string title = album ? std::string(album).substr(0, 512) : std::string();
			if (!title.empty())
				return title;
		}
	}
	std::string label = name;
	bool capital = true;
	for (char &c : label)
	{
		if (c == '-' || c == '_')
			c = ' ';
		else if (capital)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		capital = (c == ' ');
	}
	return label;
}
