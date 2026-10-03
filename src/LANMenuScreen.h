// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"

namespace GAGGUI
{
class ScreenStack;
}

class LANMenuScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "lanmenu"; }
	explicit LANMenuScreen(GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	enum
	{
		HostedGame,
		JoinedGame,
		QuitMenu
	};

  protected:
	void onEscape() override { endExecute(QuitMenu); }

  private:
	GAGGUI::ScreenStack &screens;
	void host();
	void join();
};
