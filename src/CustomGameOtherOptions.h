// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "GameHeader.h"
#include "MapHeader.h"
#include "ui/FrontendUI.h"

/// Sets the remaining game settings, like alliances, before a match.
class CustomGameOtherOptions : public Glob2UI::Screen
{
  public:
	/// Constructor, edits the given game header and map header
	CustomGameOtherOptions(GameHeader &gameHeader, MapHeader &mapHeader, bool readOnly);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	///These are the end values for this screen
	enum EndValues
	{
		Finished,
		Canceled,
	};

  protected:
	void onEscape() override;

  private:
	GameHeader &gameHeader;
	MapHeader &mapHeader;
	GameHeader oldGameHeader;
	bool readOnly;
	bool prestigeWinEnabled() const;
	void setAllyTeam(int player, int widgetIndex);
};
