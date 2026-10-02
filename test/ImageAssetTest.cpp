// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <FileManager.h>
#include <SDL.h>
#include <SDL_image.h>
#include <filesystem>
#include <fstream>
#include <cstring>
namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
const unsigned char pixels[] = {17,39,71,255,121,77,55,0,25,80,100,128,0,255,30,255};
std::string read(SDL_RWops *stream) {
    REQUIRE(stream != nullptr);
    char data[16] = {}; const auto size = SDL_RWread(stream, data, 1, sizeof(data));
    SDL_RWclose(stream); return std::string(data, size);
}
}
TEST_SUITE("ImageAssets") {
TEST_CASE("WebP decoder preserves exact RGBA including transparent RGB") {
    REQUIRE((IMG_Init(IMG_INIT_WEBP) & IMG_INIT_WEBP) != 0);
    auto stream=SDL_RWFromConstMem(webp,sizeof(webp)); REQUIRE(stream!=nullptr);
    auto surface=IMG_Load_RW(stream,1); REQUIRE(surface!=nullptr);
    auto rgba=SDL_ConvertSurfaceFormat(surface,SDL_PIXELFORMAT_RGBA32,0); REQUIRE(rgba!=nullptr);
    CHECK(rgba->w==2); CHECK(rgba->h==2);
    for(int y=0;y<2;++y) CHECK(std::memcmp(static_cast<char*>(rgba->pixels)+y*rgba->pitch,pixels+y*8,8)==0);
    SDL_FreeSurface(rgba); SDL_FreeSurface(surface);
}
TEST_CASE("Image alternatives preserve directory and original file precedence") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir scratch("image-alternatives");
    auto root=scratch.path;
    std::filesystem::create_directories(root/"first");std::filesystem::create_directories(root/"second");
    std::ofstream(root/"first"/"logical.webp")<<"first-webp";
    std::ofstream(root/"first"/"only-image.webp")<<"image-only";
    std::ofstream(root/"second"/"logical.png")<<"second-png";
    GAGCore::FileManager files("asset-tests");files.addDir((root/"first").string());files.addDir((root/"second").string());
    CHECK(read(files.openImage("logical.png"))=="first-webp");
    std::ofstream(root/"first"/"logical.png")<<"first-png";
    CHECK(read(files.openImage("logical.png"))=="first-png");
    CHECK(read(files.openImage((root/"first"/"logical.png").string()))=="first-png");
    CHECK(files.openImage("absent.png")==nullptr);
    CHECK(read(files.openImage((root/"first"/"only-image.png").string()))=="image-only");
    CHECK(files.open("only-image.png")==nullptr);
}
}
