// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include <string>
#include <vector>

// Auto-scrolling credits; dragging or the wheel takes over the scroll position.
class CreditScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "credit"; }
	CreditScreen();
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

  protected:
	void onEscape() override { endExecute(0); }
	void onEvent(const SDL_Event &event) override;

  private:
	struct Line
	{
		std::string text;
		bool decoration = false;
	};
	std::vector<Line> lines;
	bool autoScroll = true;
	Uint32 lastStep = 0;
	int walkFrame = 0;
};
