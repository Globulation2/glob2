// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <ThreadSupport.h>
#include <atomic>
#include <stdexcept>
#include <thread>
TEST_SUITE("ToolkitAssetOptions") {
TEST_CASE("pending options freeze at first use and late calls leave the live loader unchanged") {
    REQUIRE(GAGCore::Toolkit::getFileManager()==nullptr); // Run in an isolated engine process.
    glob2test::TempDir temp("toolkit-resource-options");
    GAGCore::Toolkit::init("glob2-test-toolkit-options");
    struct Close { ~Close(){ GAGCore::Toolkit::close(); } } close;
    CHECK_THROWS_AS(GAGCore::Toolkit::configureAssets({0,0,0}),std::invalid_argument);
    CHECK(GAGCore::Toolkit::configureAssets({0,0,1024}));
    std::atomic<bool> rejected=false;
    if constexpr(GAGCore::ThreadSupport::available) {
        auto wrongOwner=GAGCore::ThreadSupport::launch([&]{ try { GAGCore::Toolkit::configureAssets({0,0,1024}); }
            catch(const std::logic_error&){ rejected=true; } }); wrongOwner.join(); CHECK(rejected);
    }
    auto &loader=GAGCore::Toolkit::assets();
    CHECK_FALSE(GAGCore::Toolkit::configureAssets({0,0,2048}));
    CHECK(&GAGCore::Toolkit::assets()==&loader);
    // Existing AssetLoader tests prove external-handle retention/cancellation;
    // this fixture establishes immutable service identity and owner readiness.
}
}
