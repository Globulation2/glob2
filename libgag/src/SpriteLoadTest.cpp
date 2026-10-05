// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SpriteLoad.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <FileManager.h>
#include <fstream>
#include <thread>
#include <webp/encode.h>

namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
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
    std::vector<unsigned char> rgba(512 * 512 * 4, 255);
    unsigned char *encoded = nullptr;
    const auto size = WebPEncodeLosslessRGBA(rgba.data(), 512, 512, 512 * 4, &encoded);
    REQUIRE(size != 0);
    std::ofstream large(temp.path / "variable0.webp", std::ios::binary);
    large.write(reinterpret_cast<const char*>(encoded), size); large.close();
    WebPFree(encoded);
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
