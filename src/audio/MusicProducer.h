// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MusicStream.h"
#include <memory>
#include <vector>
namespace Music
{
// Single-owner engine. No SDL, gameplay state, threads, or device dependencies.
class Producer
{
  public:
	enum Mode
	{
		MODE_STOPPED,
		MODE_NORMAL,
		MODE_EARLY_CHANGE,
		MODE_START,
		MODE_STOP
	};
	static constexpr unsigned FadeSampleCount = 2 * GameFadeFrames;
	Mode mode = MODE_STOPPED;
	std::vector<OggOpusFile *> tracks;
	int actTrack = -1, nextTrack = -1, pendingTrack = -1;
	unsigned fadePos = 0;
	std::unique_ptr<Preview> preview;
	Producer() = default;
	~Producer();
	Producer(const Producer &) = delete;
	Producer &operator=(const Producer &) = delete;
	int load(const std::string &path, int index);
	int loadMemory(const unsigned char *bytes, size_t size, int index);
	bool replace(const std::array<std::string, 3> &paths);
	bool replaceMemory(const std::array<std::vector<unsigned char>, 3> &bytes);
	bool openPreview(const std::array<std::string, 3> &paths);
	bool openPreviewMemory(const std::array<std::vector<unsigned char>, 3> &bytes);
	void control(Control command, double value);
	void select(unsigned index, bool early, bool enabled = true);
	void stop();
	void render(std::int16_t *output, unsigned frames);
	Snapshot snapshot() const;

  private:
	// Each memory decoder borrows bytes owned here until that decoder is destroyed.
	std::vector<std::unique_ptr<std::vector<unsigned char>>> storage;
	int install(OggOpusFile *track, int index);
	void replaced();
	void commitReplacement(Producer &replacement);
};
} // namespace Music
