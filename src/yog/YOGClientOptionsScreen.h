// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "SessionTabsScreen.h"
#include <memory>
#include <string>
#include <vector>

class YOGClient;

/// Online options: the list of blocked players.
class YOGClientOptionsScreen : public SessionTab
{
  public:
	const char *recordingId() const override { return "yogclient_options"; }
	explicit YOGClientOptionsScreen(std::shared_ptr<YOGClient> client);
	std::string title() const override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onActivated() override;

	enum
	{
		QUIT,
		REMOVEBLOCKEDPLAYER,
		ADDBLOCKEDPLAYER,
	};

  private:
	void updateBlockedPlayerList();
	void addBlocked();
	void removeBlocked();
	std::shared_ptr<YOGClient> client;
	std::vector<std::string> blocked;
	int selected = -1;
	std::string draft;
};
