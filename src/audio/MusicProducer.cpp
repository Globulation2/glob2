// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "MusicProducer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
namespace Music
{
namespace
{
constexpr int InterpolationRange = 65535, InterpolationBits = 16;
}
static OggOpusFile *openMusicFile(FILE *fp)
{
	static const OpusFileCallbacks callbacks = {
		[](void *source, unsigned char *data, int size) -> int
		{
			auto *file = static_cast<FILE *>(source);
			const auto count = fread(data, 1, size, file);
			return ferror(file) ? -1 : static_cast<int>(count);
		},
		[](void *source, opus_int64 offset, int origin) -> int
		{
#ifdef _WIN32
			return _fseeki64(static_cast<FILE *>(source), offset, origin);
#else
			return fseeko(static_cast<FILE *>(source), offset, origin);
#endif
		},
		[](void *source) -> opus_int64
		{
#ifdef _WIN32
			return _ftelli64(static_cast<FILE *>(source));
#else
			return ftello(static_cast<FILE *>(source));
#endif
		},
		[](void *source) -> int { return fclose(static_cast<FILE *>(source)); }};
	int error = 0;
	auto *track = op_open_callbacks(fp, &callbacks, nullptr, 0, &error);
	if (!track)
	{
		fclose(fp);
		return nullptr;
	}
	if (!op_seekable(track) || op_link_count(track) != 1 || op_channel_count(track, 0) != 2 ||
		op_pcm_total(track, -1) <= 0)
	{
		op_free(track);
		return nullptr;
	}
	return track;
}

static void readMusic(Producer &mixer, std::int16_t *output, int count, int &index, bool advance)
{
	if (!advance && index >= 0 && static_cast<size_t>(index) < mixer.tracks.size())
	{
		Music::read(mixer.tracks[index], output, unsigned(count) / 2);
		return;
	}
	int failures = 0;
	while (count > 0)
	{
		auto *track = index >= 0 && static_cast<size_t>(index) < mixer.tracks.size()
						  ? mixer.tracks[index]
						  : nullptr;
		if (!track)
			break;
		const int frames = op_read_stereo(track, output, count);
		if (frames > 0)
		{
			output += frames * 2;
			count -= frames * 2;
			failures = 0;
			continue;
		}
		if (++failures > 4)
			break;
		if (frames == OP_HOLE)
			continue;
		if (frames < 0)
			break;
		if (advance)
			index = mixer.nextTrack;
		track = index >= 0 && static_cast<size_t>(index) < mixer.tracks.size() ? mixer.tracks[index]
																			   : nullptr;
		if (!Music::seek(track, 0))
			break;
	}
	if (count > 0)
	{
		std::fill_n(output, count, 0);
		std::cerr << "Producer: unable to decode/loop music track " << index << std::endl;
	}
}

static int fadeValue(unsigned pos, unsigned i)
{
	return fadeGain(pos + i, Producer::FadeSampleCount);
}
void Producer::render(std::int16_t *output, unsigned frames)
{
	unsigned nsamples = frames * 2;
	std::int16_t *mix = output;

	if (preview)
	{
		preview->render(output, frames);
		return;
	}
	if (actTrack < 0 || mode == MODE_STOPPED)
	{
		std::fill_n(output, frames * 2, 0);
		return;
	}
	assert(frames <= Chunk);
	assert(mode != Producer::MODE_STOPPED);
	assert(nsamples);

	if (mode == Producer::MODE_EARLY_CHANGE)
	{
		std::array<std::int16_t, Chunk * 2> pcm0{}, pcm1{};
		auto *track0 = pcm0.data();
		auto *track1 = pcm1.data();
		// Align moods once at fade start, using Opus's trimmed 48 kHz timeline.
		bool aligned = true;
		if (fadePos == 0)
			aligned = Music::seek(tracks[nextTrack], op_pcm_tell(tracks[actTrack]));
		readMusic(*this, track0, nsamples, actTrack, false);
		if (aligned)
			readMusic(*this, track1, nsamples, nextTrack, false);
		else
		{
			std::fill_n(track1, nsamples, 0);
			std::cerr << "Producer: unable to align incoming music track" << std::endl;
		}

		// mix
		for (unsigned i = 0; i < nsamples; i++)
		{
			int t0 = track0[i];
			int t1 = track1[i];
			int intI = fadeValue(fadePos, i);
			int val = (intI * t1 + ((InterpolationRange - intI) * t0)) >> InterpolationBits;
			val = (val * 255) >> 8;

			mix[i] = val;
		}

		// clear change
		fadePos += nsamples;
		if (fadePos >= FadeSampleCount)
		{
			fadePos = 0;
			actTrack = nextTrack;
			if (pendingTrack >= 0)
			{
				// a change asked for while this fade was running: cross into it
				// from the track that just landed, so the mix stays continuous.
				// Staying in MODE_EARLY_CHANGE with fadePos == 0 re-aligns the
				// incoming track on the next callback.
				nextTrack = pendingTrack;
				pendingTrack = -1;
			}
			else
				mode = Producer::MODE_NORMAL;
		}
	}
	else
	{
		readMusic(*this, mix, nsamples, actTrack, true);

		// Fades use the original fixed-point curve. Output gain is device-owned.
		if (mode == Producer::MODE_START)
		{
			for (unsigned i = 0; i < nsamples; i++)
			{
				int t = mix[i];
				t = (fadeValue(fadePos, i) * t) >> InterpolationBits;
				t = (t * 255) >> 8;

				mix[i] = t;
			}
			fadePos += nsamples;
			if (fadePos >= FadeSampleCount)
			{
				fadePos = 0;
				mode = Producer::MODE_NORMAL;
			}
		}
		else if (mode == Producer::MODE_STOP)
		{
			for (unsigned i = 0; i < nsamples; i++)
			{
				int t = mix[i];
				int intI = fadeValue(fadePos, i);
				t = ((InterpolationRange - intI) * t) >> InterpolationBits;
				t = (t * 255) >> 8;

				mix[i] = t;
			}
			fadePos += nsamples;
			if (fadePos >= FadeSampleCount)
			{
				fadePos = 0;
				mode = Producer::MODE_STOPPED;
				// The stream callback emits silence once the fade reaches MODE_STOPPED.
			}
		}
	}
}

void Producer::select(unsigned i, bool earlyChange, bool enabled)
{
	if (i >= tracks.size() || !tracks[i])
		return;

	// A fade now spans many callbacks, so a track change can be asked for while
	// one is still running — GameMusicController can emit on consecutive 40 ms
	// ticks. Restarting the fade would cut the incoming track off mid-mix, so
	// queue the request and let render() start it when this fade lands.
	if (enabled && mode == MODE_EARLY_CHANGE)
	{
		pendingTrack = static_cast<int>(i) == nextTrack ? -1 : static_cast<int>(i);
		return;
	}

	// Repeated mood events must not mix a decoder with itself.
	if (enabled && static_cast<int>(i) == actTrack && (mode == MODE_NORMAL || mode == MODE_START))
	{
		nextTrack = actTrack;
		pendingTrack = -1;
		return;
	}

	// Select next tracks. While the device is closed nothing is playing, so the
	// selection is both the current and the next track: leaving actTrack at the
	// first track ever selected would make setVolume() resume that one on
	// unmute, whatever was asked for since.
	if (enabled && actTrack >= 0)
		nextTrack = i;
	else
		nextTrack = actTrack = i;

	// Select mode
	if (enabled)
	{
		if (mode == MODE_STOPPED)
		{
			fadePos = 0;
			pendingTrack = -1;
			mode = MODE_START;
		}
		else if (earlyChange)
		{
			fadePos = 0;
			pendingTrack = -1;
			mode = MODE_EARLY_CHANGE;
		}
	}
}

namespace
{
bool valid(OggOpusFile *track)
{
	return track && op_seekable(track) && op_link_count(track) == 1 &&
		   op_channel_count(track, 0) == 2 && op_pcm_total(track, -1) > 0;
}
} // namespace
Producer::~Producer()
{
	for (auto *track : tracks)
		if (track)
			op_free(track);
}
int Producer::install(OggOpusFile *track, int index)
{
	std::unique_ptr<OggOpusFile, decltype(&op_free)> owned(track, op_free);
	if (!valid(track))
		return -2;
	if (index < 0)
		index = int(tracks.size());
	if (index > 255)
		return -2;
	if (size_t(index) >= tracks.size())
	{
		tracks.resize(index + 1);
		storage.resize(index + 1);
	}
	if (tracks[index])
		op_free(tracks[index]);
	tracks[index] = owned.release();
	storage[index].reset();
	return index;
}
int Producer::load(const std::string &path, int index)
{
	auto *file = std::fopen(path.c_str(), "rb");
	return file ? install(openMusicFile(file), index) : -1;
}
int Producer::loadMemory(const unsigned char *bytes, size_t size, int index)
{
	if (!bytes || !size || size > 16 * 1024 * 1024)
		return -2;
	auto copy = std::make_unique<std::vector<unsigned char>>(bytes, bytes + size);
	int error = 0;
	auto *track = op_open_memory(copy->data(), copy->size(), &error);
	int result = install(track, index);
	if (result >= 0)
		storage[result] = std::move(copy);
	return result;
}
void Producer::replaced()
{
	if (actTrack >= 2)
	{
		if (pendingTrack >= 2)
			actTrack = pendingTrack;
		else if (mode == MODE_EARLY_CHANGE && nextTrack >= 2)
			actTrack = nextTrack;
		nextTrack = actTrack;
		if (mode != MODE_STOPPED)
			mode = MODE_START;
	}
	fadePos = 0;
	pendingTrack = -1;
}
void Producer::commitReplacement(Producer &replacement)
{
	// Swap only after all three decoders validate; failures leave playback untouched.
	tracks.resize(std::max(size_t(5), tracks.size()));
	storage.resize(tracks.size());
	for (unsigned i = 0; i < 3; ++i)
	{
		std::swap(tracks[i + 2], replacement.tracks[i]);
		std::swap(storage[i + 2], replacement.storage[i]);
	}
	replaced();
}
bool Producer::replace(const std::array<std::string, 3> &paths)
{
	Producer replacement;
	for (unsigned i = 0; i < 3; ++i)
	{
		if (replacement.load(paths[i], i) < 0)
			return false;
		if (i && op_pcm_total(replacement.tracks[i], -1) != op_pcm_total(replacement.tracks[0], -1))
			return false;
	}
	commitReplacement(replacement);
	return true;
}
bool Producer::replaceMemory(const std::array<std::vector<unsigned char>, 3> &bytes)
{
	Producer replacement;
	for (unsigned i = 0; i < 3; ++i)
	{
		if (replacement.loadMemory(bytes[i].data(), bytes[i].size(), i) < 0)
			return false;
		if (i && op_pcm_total(replacement.tracks[i], -1) != op_pcm_total(replacement.tracks[0], -1))
			return false;
	}
	commitReplacement(replacement);
	return true;
}
bool Producer::openPreview(const std::array<std::string, 3> &paths)
{
	auto value = std::make_unique<Preview>();
	if (!value->open(paths))
		return false;
	preview = std::move(value);
	return true;
}
bool Producer::openPreviewMemory(const std::array<std::vector<unsigned char>, 3> &bytes)
{
	auto value = std::make_unique<Preview>();
	for (unsigned i = 0; i < 3; ++i)
		if (!value->openMemory(i, bytes[i].data(), bytes[i].size()))
			return false;
	preview = std::move(value);
	return true;
}
void Producer::stop()
{
	fadePos = 0;
	pendingTrack = -1;
	mode = MODE_STOP;
}
void Producer::control(Control command, double value)
{
	if (!preview)
		return;
	switch (command)
	{
	case Control::Play:
		preview->playing = value != 0;
		break;
	case Control::Mood:
		preview->setMood(unsigned(value));
		break;
	case Control::Seek:
		preview->seekTo(value);
		break;
	case Control::Fade:
		preview->setFade(value);
		break;
	case Control::Blend:
		preview->setBlend(value);
		break;
	case Control::Audition:
		preview->audition = value != 0;
		break;
	case Control::Reset:
		preview->reset();
		break;
	}
}
Snapshot Producer::snapshot() const
{
	Snapshot s;
	s.track = actTrack;
	s.next = nextTrack;
	s.pending = pendingTrack;
	s.mode = mode;
	s.fade = fadePos;
	if (preview)
	{
		s.preview = true;
		s.playing = preview->playing;
		s.audition = preview->audition;
		s.failed = preview->failed();
		s.position = preview->position();
		s.duration = preview->duration();
		s.weights = preview->weights();
	}
	else if (actTrack >= 0 && size_t(actTrack) < tracks.size() && tracks[actTrack])
	{
		s.playing = mode != MODE_STOPPED;
		s.position = double(op_pcm_tell(tracks[actTrack])) / Rate;
		s.duration = double(op_pcm_total(tracks[actTrack], -1)) / Rate;
	}
	return s;
}
} // namespace Music
