// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include "FertilityCalculator.h"
namespace GAGGUI { class ProgressBar; }
class FertilityScreen : public Glob2Screen
{
public:
    explicit FertilityScreen(Map& map);
    void onTimer(Uint32) override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override;
    Uint32 executionDelay(Uint32, Uint32) override { return 1; }
private:
    FertilityCalculator::Job job;
    GAGGUI::ProgressBar* progress;
};
