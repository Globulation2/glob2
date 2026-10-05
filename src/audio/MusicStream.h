// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <opusfile.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Music
{
constexpr int Rate = 48000;
constexpr unsigned Chunk = 1024;
constexpr unsigned GameFadeFrames = (16384 * Rate + 22050) / 44100;
// Shared by gameplay and previews. These never allocate PCM proportional to duration.
bool seek(OggOpusFile *track, ogg_int64_t frame);
bool read(OggOpusFile *track, std::int16_t *output, unsigned frames);
int fadeGain(unsigned sample, unsigned sampleCount);

class Preview
{
  public:
	~Preview();
	bool open(const std::array<std::string, 3> &paths);
	bool openMemory(unsigned mood, const unsigned char *bytes, size_t size);
	bool ready() const;
	void close();
	void render(std::int16_t *output, unsigned frames);
	bool seekTo(double seconds);
	void setMood(unsigned mood);
	void setBlend(double value);
	void reset();
	void setFade(double seconds);
	double position() const { return double(cursor) / Rate; }
	double duration() const { return double(length) / Rate; }
	std::array<double, 3> weights() const { return gains; }
	bool playing = false, audition = false;
	bool failed() const { return decodeFailed; }

  private:
	std::array<OggOpusFile *, 3> tracks{};
	std::array<std::vector<unsigned char>, 3> compressed;
	std::array<std::array<std::int16_t, Chunk * 2>, 3> pcm{};
	std::array<double, 3> gains{1, 0, 0}, from{1, 0, 0}, target{1, 0, 0};
	ogg_int64_t cursor = 0, length = 0;
	unsigned fadeFrames = GameFadeFrames, fadeAt = 0, current = 0;
	int pending = -1;
	std::uint64_t auditionFrames = 0;
	bool decodeFailed = false;
};
} // namespace Music
