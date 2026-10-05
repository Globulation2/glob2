#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Glob2Test.h"
#include "LossyAlphaFixture.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#line 41 "libgag/src/ImageAssetTest.cpp"
TEST_SUITE("ImageAssets") {
TEST_CASE("16-bit RGBA rounds normalized channels to the exporter reference") {
    // Keep one JUnit result for the strict native selection inventory while
    // exercising both production decoder paths against the same reference.
    for (const auto decoder : {IMG_Load_IO, SDL_LoadPNG_IO}) {
        auto surface = decoder(SDL_IOFromConstMem(rgba16Fixture::png, sizeof(rgba16Fixture::png)), true);
        REQUIRE(surface != nullptr);
        auto rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(rgba != nullptr);
        CHECK(rgba->w == 2); CHECK(rgba->h == 2);
        for (int y = 0; y < 2; ++y)
            CHECK(std::memcmp(static_cast<char*>(rgba->pixels) + y * rgba->pitch,
                              rgba16Fixture::rgba + y * 8, 8) == 0);
        SDL_DestroySurface(rgba); SDL_DestroySurface(surface);
    }
}
}
