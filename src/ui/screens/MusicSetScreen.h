// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "MusicLibrary.h"
#include "MusicStream.h"
#include <functional>
#include "ui/OnlineUI.h"
class MusicSetScreen : public Glob2UI::Screen
{
  public:
	MusicSetScreen(Music::Metadata info, const std::array<std::string, 3> &paths,
				   std::string primaryAction = {});
	~MusicSetScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;
	const char *recordingId() const override { return "music_set"; }

  protected:
	bool panel() const override { return false; }
	void onEscape() override { endExecute(0); }
	bool interceptEvent(const SDL_Event &) override;

  private:
	Music::Metadata info;
	Music::Preview preview;
	std::string notice, primaryAction;
	int blend = 0, fadeMs = 371;
	std::array<std::string, 3> paths;
	OggOpusFile *waveformDecoder = nullptr;
	unsigned waveformMood = 0;
	std::int64_t waveformFrame = 0;
	Glob2UI::PreviewImages images;
	bool suspended = false;
	void control(const std::function<void()> &fn);
};
