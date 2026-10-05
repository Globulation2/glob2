// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicStream.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace Music
{
bool seek(OggOpusFile *track, ogg_int64_t frame)
{
	// opusfile 0.12 can retain old PCM after a short forward seek.
	return track && frame >= 0 && op_raw_seek(track, 0) == 0 && op_pcm_seek(track, frame) == 0;
}
bool read(OggOpusFile *track, std::int16_t *output, unsigned frames)
{
	unsigned failures = 0;
	while (track && frames)
	{
		int got = op_read_stereo(track, output, int(frames * 2));
		if (got > 0)
		{
			output += got * 2;
			frames -= got;
			failures = 0;
			continue;
		}
		if (++failures > 4)
			break;
		if (got == OP_HOLE)
			continue;
		if (got < 0 || !seek(track, 0))
			break;
	}
	if (frames)
		std::fill_n(output, frames * 2, 0);
	return !frames;
}
int fadeGain(unsigned sample, unsigned sampleCount)
{
	// Retain the game's polynomial evaluation and 16-bit rounding exactly.
	double l = std::max(1u, sampleCount - 1), x = std::min(sample, sampleCount - 1);
	double a = -2 / (l * l * l), b = 3 / (l * l);
	return int(65535 * (a * (x * x * x) + b * (x * x)));
}
Preview::~Preview()
{
	close();
}
void Preview::close()
{
	playing = false;
	for (unsigned i = 0; i < 3; ++i)
	{
		if (tracks[i])
			op_free(tracks[i]);
		tracks[i] = nullptr;
		std::vector<unsigned char>().swap(compressed[i]);
	}
	cursor = length = 0;
}
static bool valid(OggOpusFile *file)
{
	return file && op_seekable(file) && op_link_count(file) == 1 &&
		   op_channel_count(file, 0) == 2 && op_pcm_total(file, -1) > 0;
}
bool Preview::open(const std::array<std::string, 3> &paths)
{
	close();
	for (unsigned i = 0; i < 3; ++i)
	{
		int error = 0;
		tracks[i] = op_open_file(paths[i].c_str(), &error);
		if (!valid(tracks[i]))
		{
			close();
			return false;
		}
		if (!i)
			length = op_pcm_total(tracks[i], -1);
		if (length != op_pcm_total(tracks[i], -1))
		{
			close();
			return false;
		}
	}
	reset();
	return true;
}
bool Preview::openMemory(unsigned mood, const unsigned char *bytes, size_t size)
{
	if (mood >= 3 || !bytes || !size || size > 16 * 1024 * 1024)
		return false;
	if (tracks[mood])
	{
		op_free(tracks[mood]);
		tracks[mood] = nullptr;
	}
	compressed[mood].assign(bytes, bytes + size);
	int error = 0;
	tracks[mood] = op_open_memory(compressed[mood].data(), size, &error);
	if (!valid(tracks[mood]))
	{
		close();
		return false;
	}
	if (!length)
		length = op_pcm_total(tracks[mood], -1);
	if (length != op_pcm_total(tracks[mood], -1))
	{
		close();
		return false;
	}
	return true;
}
bool Preview::ready() const
{
	return tracks[0] && tracks[1] && tracks[2] && length > 0;
}
void Preview::setFade(double seconds)
{
	if (std::isfinite(seconds))
		fadeFrames = std::max(1u, unsigned(std::clamp(seconds, 0.0, 10.0) * Rate));
}
void Preview::reset()
{
	fadeFrames = GameFadeFrames;
	fadeAt = 0;
	pending = -1;
	current = 0;
	gains = from = target = {1, 0, 0};
	audition = false;
	auditionFrames = 0;
	decodeFailed = false;
}
void Preview::setMood(unsigned mood)
{
	if (mood >= 3)
		return;
	if (fadeAt)
	{
		pending = int(mood);
		return;
	}
	if (gains[mood] == 1)
		return;
	from = gains;
	target = {0, 0, 0};
	target[mood] = 1;
	current = mood;
	fadeAt = 1;
}
void Preview::setBlend(double value)
{
	if (!std::isfinite(value))
		return;
	value = std::clamp(value, 0.0, 2.0);
	fadeAt = 0;
	pending = -1;
	gains = {std::max(0.0, 1 - value), 1 - std::abs(value - 1), std::max(0.0, value - 1)};
	from = target = gains;
}
bool Preview::seekTo(double seconds)
{
	if (!ready() || !std::isfinite(seconds))
		return false;
	auto frame = ogg_int64_t(std::clamp(seconds, 0.0, duration()) * Rate) % length;
	for (auto *track : tracks)
		if (!seek(track, frame))
		{
			decodeFailed = true;
			playing = false;
			return false;
		}
	cursor = frame;
	return true;
}
void Preview::render(std::int16_t *output, unsigned frames)
{
	if (!playing || !ready())
	{
		std::fill_n(output, frames * 2, 0);
		return;
	}
	while (frames)
	{
		unsigned count = std::min(Chunk, frames);
		for (unsigned mood = 0; mood < 3; ++mood)
			if (!read(tracks[mood], pcm[mood].data(), count))
				decodeFailed = true;
		if (decodeFailed)
		{
			playing = false;
			std::fill_n(output, frames * 2, 0);
			return;
		}
		for (unsigned f = 0; f < count; ++f)
		{
			for (unsigned channel = 0; channel < 2; ++channel)
			{
				if (fadeAt)
				{
					double t = double(fadeGain((fadeAt - 1) * 2 + channel, fadeFrames * 2)) / 65535;
					for (unsigned mood = 0; mood < 3; ++mood)
						gains[mood] = from[mood] + (target[mood] - from[mood]) * t;
				}
				double mixed = 0;
				for (unsigned mood = 0; mood < 3; ++mood)
					mixed += pcm[mood][f * 2 + channel] * gains[mood];
				output[f * 2 + channel] = std::int16_t(std::clamp(mixed, -32768.0, 32767.0));
			}
			if (fadeAt && ++fadeAt > fadeFrames)
			{
				fadeAt = 0;
				gains = target;
				if (pending >= 0)
				{
					unsigned mood = unsigned(pending);
					pending = -1;
					setMood(mood);
				}
			}
			if (audition && ++auditionFrames >= Rate * 8)
			{
				auditionFrames = 0;
				setMood((current + 1) % 3);
			}
		}
		cursor = (cursor + count) % length;
		output += count * 2;
		frames -= count;
	}
}
} // namespace Music
