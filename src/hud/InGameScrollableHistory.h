// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "GameGUIMessageManager.h"
#include "ui/FrontendUI.h"

#include <list>

///The scrolling chat history, shown above the chat area until closed.
class InGameScrollableHistory : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_scrollable_history"; }

  public:
	explicit InGameScrollableHistory(const std::list<InGameMessage>& messageHistory);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

protected:
	bool scrim() const override { return false; }
	void onEscape() override { finish(0); }
	void onUpdate(Uint32 tick) override;
	GAGGUI::ui::Rect place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area) override;
	/// The list of messages
	const std::list<InGameMessage>& history;
	/// The last known size of the history, to count for changes
	size_t lastSize = 0;
};
