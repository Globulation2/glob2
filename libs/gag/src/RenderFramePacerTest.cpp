// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RenderFramePacer.h>
#include <algorithm>

TEST_SUITE("RenderFramePacer")
{
    TEST_CASE("caps starts precisely without catching up missed frames")
    {
        for (int fps : {25, 30, 60, 90, 120, 144, 165, 240}) {
            GAGCore::RenderFramePacer pacer;
            pacer.configure(fps);
            const std::uint64_t period = (1000000000ULL + fps - 1) / fps;
            CHECK(pacer.begin(0));
            for (std::uint64_t frame = 1; frame <= 10000; ++frame) {
                CHECK_FALSE(pacer.begin(frame * period - 1));
                CHECK(pacer.begin(frame * period));
            }
            CHECK(pacer.begin(1000000000000ULL));
            CHECK_FALSE(pacer.begin(1000000000000ULL));
            CHECK_FALSE(pacer.begin(1000000000000ULL + period - 1));
            CHECK(pacer.begin(1000000000000ULL + period));
        }
    }
    TEST_CASE("display callbacks above and below the target retain the available rate")
    {
        for (int fps : {25, 60, 120, 144, 240})
            for (int refresh : {25, 30, 60, 144, 240}) {
                GAGCore::RenderFramePacer pacer;
                pacer.configure(fps);
                int frames = 0;
                for (int callback = 0; callback <= refresh; ++callback)
                    if (pacer.begin(std::uint64_t(callback) * 1000000000ULL / refresh)) ++frames;
                INFO("target=" << fps << " display=" << refresh);
                CHECK(frames <= std::min(fps, refresh) + 1);
                CHECK(frames >= std::min(fps, refresh) - 1);
            }
    }
    TEST_CASE("minor scheduling jitter preserves the target phase")
    {
        GAGCore::RenderFramePacer pacer;
        pacer.configure(60);
        CHECK(pacer.begin(0));
        CHECK(pacer.begin(17000000));
        CHECK(pacer.begin(34000000));
        CHECK_FALSE(pacer.begin(50000000));
        CHECK(pacer.begin(50000001));
        CHECK(pacer.waitMilliseconds(50000001) == 17);
        // A missed full interval discards the old phase instead of catching up.
        CHECK(pacer.begin(1000000000));
        CHECK_FALSE(pacer.begin(1000000001));
    }
    TEST_CASE("waits account for frame cost; live changes reset; unlimited never waits")
    {
        GAGCore::RenderFramePacer pacer;
        pacer.configure(60);
        CHECK(pacer.begin(1000000000));
        CHECK(pacer.waitMilliseconds(1000000000) == 17);
        CHECK(pacer.waitMilliseconds(1010000000) == 7);
        CHECK(pacer.waitMilliseconds(1020000000) == 0);
        pacer.configure(25);
        CHECK(pacer.begin(1020000000));
        CHECK(pacer.waitMilliseconds(1020000000) == 40);
        pacer.configure(25); // Reapplying the same preference must not uncap.
        CHECK_FALSE(pacer.begin(1020000000));
        pacer.reset();
        CHECK(pacer.begin(1020000000));
        pacer.configure(0);
        CHECK(pacer.begin(1020000000));
        CHECK(pacer.begin(1020000000));
        CHECK(pacer.waitMilliseconds(1020000000) == 0);
    }

    TEST_CASE("paced screens wait for the next frame, wake for input and do not spin while idle")
    {
        using GAGCore::InputWakeMilliseconds;
        using GAGCore::pacedScreenWait;
        constexpr std::uint64_t ms = 1000000;
        GAGCore::RenderFramePacer pacer;
        pacer.configure(60);
        CHECK(pacer.capped());
        REQUIRE(pacer.begin(1000 * ms));
        // Early in a fast frame: input still wakes the screen within 8 ms.
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1001 * ms)) == InputWakeMilliseconds);
        // Close to the next frame: wake exactly when it is due.
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1012 * ms)) == 5);
        // A frame slower than the period is followed at once, idle or not:
        // capped idle frames still keep animation at the target rate.
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1020 * ms)) == 0);
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1020 * ms), false, true) == 0);
        // Unlimited: no wait with work, the input wake while idle.
        pacer.configure(0);
        CHECK_FALSE(pacer.capped());
        REQUIRE(pacer.begin(1020 * ms));
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1020 * ms), true, false) == 0);
        CHECK(pacedScreenWait(pacer.waitMilliseconds(1020 * ms), true, true) == InputWakeMilliseconds);
    }
}
