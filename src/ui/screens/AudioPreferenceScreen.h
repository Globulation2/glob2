// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"

class AudioPreferenceScreen : public Glob2UI::Screen
{
  public:
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void choose(bool sound);

  protected:
	void onEscape() override {} // Dismissing input never enables sound.

  private:
	bool saveFailed = false;
};
