// SPDX-License-Identifier: GPL-3.0-or-later
#include "FertilityScreen.h"
#include <GUIProgressBar.h>
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
FertilityScreen::FertilityScreen(Map& map) : job(map)
{
    auto& strings = *GAGCore::Toolkit::getStringTable();
    addWidget(new GAGGUI::Text(0, 160, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard",
        strings.getString("[Computing Fertility]")));
    progress = new GAGGUI::ProgressBar(120, 220, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 1000);
    addWidget(progress);
    addWidget(new GAGGUI::TextButton(230, 340, 180, 40, ALIGN_SCREEN_CENTERED,
        ALIGN_SCREEN_CENTERED, "menu", strings.getString("[Cancel]"), 0, 27));
}
void FertilityScreen::onTimer(Uint32)
{
    if (job.advance(65536)) { job.commit(); endExecute(1); }
    progress->setValue(static_cast<int>(job.progress() * 1000));
}
void FertilityScreen::onAction(GAGGUI::Widget*, GAGGUI::Action action, int, int)
{
    if (action == GAGGUI::BUTTON_RELEASED || action == GAGGUI::BUTTON_SHORTCUT) endExecute(0);
}
