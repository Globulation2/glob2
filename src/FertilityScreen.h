// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FertilityCalculator.h"
#include "ui/FrontendUI.h"

class FertilityScreen : public Glob2UI::Screen
{
  public:
	explicit FertilityScreen(Map &map);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32) override;
	Uint32 executionDelay(Uint32, Uint32) override { return 1; }

  protected:
	void onEscape() override { endExecute(0); }

  private:
	bool presented = false;
	FertilityCalculator::Job job;
	int permille = 0;
};
