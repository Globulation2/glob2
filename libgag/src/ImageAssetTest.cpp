// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "LossyAlphaFixture.h"
#include <FileManager.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <cstring>
namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
const unsigned char pixels[] = {17,39,71,255,121,77,55,0,25,80,100,128,0,255,30,255};
std::string read(SDL_IOStream *stream) {
    REQUIRE(stream != nullptr);
    char data[16] = {}; const auto size = SDL_ReadIO(stream, data, sizeof(data));
    SDL_CloseIO(stream); return std::string(data, size);
}
}
TEST_SUITE("ImageAssets") {
TEST_CASE("WebP decoder preserves exact RGBA including transparent RGB") {
    auto stream=SDL_IOFromConstMem(webp,sizeof(webp)); REQUIRE(stream!=nullptr);
    auto surface=IMG_Load_IO(stream,1); REQUIRE(surface!=nullptr);
    auto rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32); REQUIRE(rgba!=nullptr);
    CHECK(rgba->w==2); CHECK(rgba->h==2);
    for(int y=0;y<2;++y) CHECK(std::memcmp(static_cast<char*>(rgba->pixels)+y*rgba->pitch,pixels+y*8,8)==0);
    SDL_DestroySurface(rgba); SDL_DestroySurface(surface);
}
TEST_CASE("Q90 lossy WebP preserves dimensions and exact alpha") {
    using namespace lossyAlphaFixture;
    auto surface = IMG_Load_IO(SDL_IOFromConstMem(lossyAlphaFixture::webp, sizeof(lossyAlphaFixture::webp)), true);
    REQUIRE(surface != nullptr);
    CHECK(surface->w == width); CHECK(surface->h == height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            Uint8 r, g, b, a;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            CHECK(a == alpha[y * width + x]);
        }
    SDL_DestroySurface(surface);
}
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
#ifndef __EMSCRIPTEN__
TEST_CASE("Native PNG and JPEG loading and saving remain available") {
    glob2test::TempDir scratch("image-save-codecs");
    auto surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    for (int y = 0; y < 2; ++y)
        std::memcpy(static_cast<char*>(surface->pixels) + y * surface->pitch, pixels + y * 8, 8);
    const auto png = (scratch.path / "image.png").string();
    const auto jpeg = (scratch.path / "image.jpg").string();
    REQUIRE(IMG_SavePNG(surface, png.c_str()));
    REQUIRE(IMG_SaveJPG(surface, jpeg.c_str(), 100));
    SDL_DestroySurface(surface);
    // Match FileManager's stream-based image loading. macOS's filename-only
    // ImageIO path premultiplies RGB, losing invisible colors and rounding
    // partial alpha; the configured PNG stream decoder remains lossless.
    auto stream = SDL_IOFromFile(png.c_str(), "rb");
    REQUIRE(stream != nullptr);
    auto loaded = IMG_Load_IO(stream, true);
    REQUIRE(loaded != nullptr);
    auto rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(rgba != nullptr);
    for (int y = 0; y < 2; ++y)
        CHECK(std::memcmp(static_cast<char*>(rgba->pixels) + y * rgba->pitch, pixels + y * 8, 8) == 0);
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(loaded);
    loaded = IMG_Load(jpeg.c_str());
    REQUIRE(loaded != nullptr);
    CHECK(loaded->w == 2);
    CHECK(loaded->h == 2);
    SDL_DestroySurface(loaded);
}
#endif
TEST_CASE("WebP artwork lookup preserves directories without PNG alternatives") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir scratch("image-artwork");
    auto root=scratch.path;
    std::filesystem::create_directories(root/"first");std::filesystem::create_directories(root/"second");
    std::ofstream(root/"first"/"logical.webp")<<"first-webp";
    std::ofstream(root/"first"/"logical.png")<<"first-png";
    std::ofstream(root/"second"/"logical.webp")<<"second-webp";
    std::ofstream(root/"first"/"png-only.png")<<"png-only";
    GAGCore::FileManager files("asset-tests");files.addDir((root/"first").string());files.addDir((root/"second").string());
    CHECK(read(files.openImage("logical.webp"))=="first-webp");
    CHECK(read(files.openImage((root/"first"/"logical.webp").string()))=="first-webp");
    CHECK(files.openImage("png-only.webp")==nullptr);
    // External previews and imports still open their exact PNG names.
    CHECK(read(files.openImage("png-only.png"))=="png-only");
    CHECK(files.openImage((root/"second"/"logical.png").string())==nullptr);
}
}
