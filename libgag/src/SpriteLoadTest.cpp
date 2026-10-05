// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SpriteLoad.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <FileManager.h>
#include <fstream>
#include <thread>

namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
// A 512x512 lossless WebP keeps this allocation regression decoder-only.
const unsigned char largeWebp[] = {
    82,73,70,70,186,1,0,0,87,69,66,80,86,80,56,76,
    174,1,0,0,47,255,193,127,0,63,64,152,109,100,178,247,
    200,143,105,4,199,68,38,96,241,75,34,185,76,107,160,129,
    23,89,128,137,201,238,238,72,66,12,107,56,243,31,255,86,
    77,21,114,23,34,208,1,73,181,45,9,82,148,56,32,44,
    36,14,8,11,129,131,9,11,57,14,72,255,139,60,172,238,
    58,34,250,111,180,109,155,14,152,238,128,92,144,207,237,231,
    188,63,206,115,247,57,239,143,103,95,207,190,189,63,158,253,
    62,251,239,253,193,155,188,135,55,121,15,111,242,30,222,228,
    61,188,201,123,120,147,247,240,38,239,225,77,222,195,155,188,
    135,55,121,15,111,242,30,222,228,61,188,201,123,120,147,247,
    240,38,239,225,253,226,253,229,221,188,151,119,243,94,222,205,
    123,121,55,239,229,221,188,151,119,243,94,222,205,123,121,55,
    239,229,221,188,151,119,243,94,222,205,123,121,55,239,229,221,
    188,151,119,243,222,241,191,59,24,255,187,131,241,191,59,24,
    255,187,131,241,191,59,24,255,187,131,241,191,59,24,255,187,
    131,241,191,59,24,255,187,131,241,191,59,24,255,187,131,191,
    186,131,241,191,59,24,255,187,131,120,110,61,231,253,209,207,
    213,115,222,31,207,62,158,125,122,127,60,251,121,246,207,251,
    131,55,120,155,55,120,155,55,120,155,55,120,155,55,120,155,
    55,120,155,55,120,155,55,120,155,55,120,155,55,120,155,55,
    120,155,55,120,155,55,120,155,55,120,155,55,120,155,247,131,
    247,135,119,241,22,239,226,45,222,197,91,188,139,183,120,23,
    111,241,46,222,226,93,188,197,187,120,139,119,241,22,239,226,
    45,222,197,91,188,139,183,120,23,111,241,46,222,26,255,187,
    131,241,191,59,24,255,187,131,241,191,59,24,255,187,131,241,
    191,59,24,255,187,131,241,191,59,24,255,187,131,241,191,59,
    24,255,187,131,241,191,59,248,163,59,24,255,187,131,241,191,
    59,40,
};
void image(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(webp), sizeof(webp));
}
std::unique_ptr<GAGCore::Sprite> finish(GAGCore::SpriteLoad& load) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!load.poll() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    REQUIRE_MESSAGE(!load.failed(), load.error());
    auto sprite = load.take(); REQUIRE(sprite); return sprite;
}
}
TEST_SUITE("SpriteLoad") {
TEST_CASE("source frames prepare in parallel without a graphic context") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir temp("sprite-load");
    image(temp.path / "test0.webp"); image(temp.path / "test1r.webp");
    GAGCore::Toolkit::getFileManager()->addDir(temp.path.string());
    GAGCore::SpriteLoad loading("test");
    CHECK_FALSE(loading.poll(std::chrono::milliseconds::zero()));
    CHECK(loading.completed() == 0);
    auto sprite = finish(loading);
    CHECK(sprite->getFrameCount() == 2);
    CHECK(sprite->getW(0) == 2); CHECK(sprite->getH(1) == 2);
    CHECK(sprite->nativeFrame(0) != nullptr); CHECK(sprite->nativeFrame(1) == nullptr);
}
TEST_CASE("sheet tiles retain frame order and malformed sheets fall back to WebP frames") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir temp("sheet-load");
    image(temp.path / "sheet.webp"); image(temp.path / "fallback0.webp");
    std::ofstream(temp.path / "packed.sheet") << "sheet.webp image 0 2 1 2\n";
    std::ofstream(temp.path / "fallback.sheet") << "sheet.webp image 0 3 2 2\n";
    GAGCore::Toolkit::getFileManager()->addDir(temp.path.string());
    GAGCore::SpriteLoad packed("packed"), fallback("fallback");
    auto first = finish(packed), second = finish(fallback);
    CHECK(first->getFrameCount() == 2); CHECK(first->getW(1) == 1);
    const auto *surface = first->nativeFrame(1)->getSDLSurface();
    CHECK(*static_cast<const Uint32*>(surface->pixels) == 0x00794d37u); // exact invisible RGB
    CHECK(second->getFrameCount() == 1); CHECK(second->getW(0) == 2);
}
#ifdef HAVE_OPENGL
TEST_CASE("variable frame atlas admission accounts for maximum sized padded cells [display]") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir temp("variable-atlas-admission");
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::USEGPU, "variable atlas admission");
    std::ofstream large(temp.path / "variable0.webp", std::ios::binary);
    large.write(reinterpret_cast<const char*>(largeWebp), sizeof(largeWebp));
    large.close();
    for (unsigned frame = 1; frame < 16; ++frame) image(temp.path / ("variable" + std::to_string(frame) + ".webp"));
    GAGCore::Toolkit::getFileManager()->addDir(temp.path.string());
    GAGCore::SpriteLoad loading("variable", true);
    auto sprite = finish(loading);
    CHECK(sprite->getFrameCount() == 16);
    CHECK(sprite->nativeFrame(0)->getW() == 512);
    CHECK(sprite->nativeFrame(15)->getW() == 2);
    // Only one input is large, but all sixteen atlas cells must accommodate it.
    CHECK(GAGCore::Toolkit::assets().metrics().peakWorkingBytes >= size_t(514 * 4) * (514 * 4) * 4);
}
#endif

}
