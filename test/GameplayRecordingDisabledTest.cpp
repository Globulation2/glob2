// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "SettingsScreen.h"
#include "ui/RecordingControls.h"
#include <algorithm>

TEST_SUITE("GameplayRecording.DisabledUI")
{
    TEST_CASE("codec-free packages hide the recording category and menu offer")
    {
        glob2test::HeadlessGlobals globals;
        CHECK_FALSE(Glob2UI::recordingOffered());
        SettingsScreen settings;
        const auto categories = settings.visibleCategories();
        CHECK(std::find(categories.begin(), categories.end(), SettingsScreen::Category::Recording) == categories.end());
    }
}
