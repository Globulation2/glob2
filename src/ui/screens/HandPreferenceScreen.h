// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"

class HandPreferenceScreen : public Glob2UI::Screen
{
  public:
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void choose(int side);

  protected:
	void onEscape() override {} // Both choices complete the first-launch prompt.

  private:
	bool saveFailed = false;
};
