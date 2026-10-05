// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MusicTypes.h"
#include <string>
class BrowserMusic
{
	int nextIndex = 0;

  public:
	~BrowserMusic();
	int load(const std::string &path, int index);
	bool replace(const std::array<std::string, 3> &paths);
	bool preview(const std::array<std::string, 3> &paths);
	void select(unsigned index, bool early);
	void volume(unsigned music, unsigned voice, bool mute);
	void command(const char *type);
	void control(Music::Control command, double value);
	bool enabled() const;
	Music::Snapshot snapshot() const;
	Music::Diagnostics diagnostics() const;
};
