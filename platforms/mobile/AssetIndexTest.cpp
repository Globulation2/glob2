// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetIndex.h"
#include "Glob2Test.h"
#include <memory>

TEST_SUITE("MobileAssetIndex")
{
    TEST_CASE("current artwork inventory can exceed the previous one MiB startup bound")
    {
        std::string index(64, '0');
        index += '\n';
        for (unsigned i = 0; i < 32000; ++i)
            index += "data/gfx/terrain/catalogue/material/frame-" + std::to_string(i) + ".png\n";
        REQUIRE(index.size() > 1024 * 1024);
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(
            SDL_IOFromConstMem(index.data(), index.size()), SDL_CloseIO);
        REQUIRE(stream);
        CHECK(MobileAssetIndex::read(stream.get()) == index);
    }

    TEST_CASE("oversized packaged indexes remain rejected before reading")
    {
        const std::string index(4 * 1024 * 1024 + 1, 'x');
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(
            SDL_IOFromConstMem(index.data(), index.size()), SDL_CloseIO);
        REQUIRE(stream);
        CHECK_THROWS_WITH(MobileAssetIndex::read(stream.get()), "Invalid asset index size");
        CHECK(SDL_TellIO(stream.get()) == 0);
    }
}
