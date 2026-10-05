// SPDX-License-Identifier: GPL-3.0-or-later
// GAGCore::Sprite's sheet loader (the <name>.sheet index tools/package_assets.py
// writes for packaged builds) and its fallback to one file per frame.
#include "Glob2Test.h"
#include "LossyAlphaFixture.h"
#include "SpritePatternFixture.h"
#include "ScopedEnvironment.h"
#include <FileManager.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
// Frame i's pixel at (x, y): position dependent, with partial and zero alpha.
SDL_Color patternPixel(int frame, int x, int y)
{
	return SDL_Color{static_cast<Uint8>(frame * 10), static_cast<Uint8>(x * 20 + 5),
		static_cast<Uint8>(y * 30 + 7), static_cast<Uint8>(frame == 1 ? 0 : 255 - frame)};
}

void writePattern(const std::filesystem::path &path, int first, int count, int columns, int tile)
{

    using namespace spritePatternFixture;
    const unsigned char *data = nullptr;
    size_t size = 0;
    if (first == 0 && count == 5 && columns == 2 && tile == 3) { data = pattern_0_5_2_3; size = sizeof(pattern_0_5_2_3); }
    if (first == 2 && count == 2 && columns == 4 && tile == 4) { data = pattern_2_2_4_4; size = sizeof(pattern_2_2_4_4); }
    if (first == 0 && count == 1 && columns == 1 && tile == 4) { data = pattern_0_1_1_4; size = sizeof(pattern_0_1_1_4); }
    if (first == 1 && count == 1 && columns == 1 && tile == 4) { data = pattern_1_1_1_4; size = sizeof(pattern_1_1_1_4); }
    if (first == 3 && count == 1 && columns == 1 && tile == 4) { data = pattern_3_1_1_4; size = sizeof(pattern_3_1_1_4); }
    if (first == 0 && count == 4 && columns == 2 && tile == 3) { data = pattern_0_4_2_3; size = sizeof(pattern_0_4_2_3); }
    REQUIRE(data != nullptr);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);

}

bool samePixels(GAGCore::DrawableSurface *surface, int frame, int size)
{
	if (!surface || surface->getW() != size || surface->getH() != size)
		return false;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			Uint8 r, g, b, a;
			if (!SDL_ReadSurfacePixel(surface->sdlsurface, x, y, &r, &g, &b, &a))
				return false;
			const SDL_Color c = patternPixel(frame, x, y);
			if (r != c.r || g != c.g || b != c.b || a != c.a)
				return false;
		}
	return true;
}

struct SheetFixture
{
	glob2test::ToolkitScope toolkit;
	glob2test::TempDir scratch{"sprite-sheets"};
	SheetFixture()
	{
		GAGCore::Toolkit::initGraphic(64, 64, 0, "sprite sheet loader");
		GAGCore::Toolkit::getFileManager()->addDir(scratch.path.string());
	}
	void index(const std::string &name, const std::string &text)
	{
		std::ofstream(scratch.path / (name + ".sheet"), std::ios::binary) << text;
	}
};
}

TEST_SUITE("SpriteSheets") {
TEST_CASE("sheet tiles load as the frames they were packed from") {
	SheetFixture fixture;
	// Five recolorable 3x3 frames, sixteen to a row in the packer but two here,
	// and two plain 4x4 frames starting at frame 2 in a wider grid.
	writePattern(fixture.scratch.path / "set-sheet-0.webp", 0, 5, 2, 3);
	writePattern(fixture.scratch.path / "set-sheet-1.webp", 2, 2, 4, 4);
	// Windows line endings, as a text-mode save on Windows writes them.
	fixture.index("set", "# comment\r\n\r\nset-sheet-0.webp rotated 0 5 3 3\r\nset-sheet-1.webp image 2 2 4 4\r\n");
	GAGCore::Sprite sprite;
	REQUIRE(sprite.load("set"));
	REQUIRE(sprite.getFrameCount() == 5);
	for (int i = 0; i < 5; ++i)
	{
		const bool plain = i == 2 || i == 3;
		CHECK(sprite.rotated[i] != nullptr);
		CHECK((sprite.images[i] != nullptr) == plain);
		GLOB2_CHECK(samePixels(sprite.rotated[i]->orig, i, 3), "recolorable frame " + std::to_string(i));
		if (plain)
			GLOB2_CHECK(samePixels(sprite.images[i], i, 4), "plain frame " + std::to_string(i));
		// getW/getH prefer the plain layer, so both sheets' sizes show.
		CHECK(sprite.getW(i) == (plain ? 4 : 3));
	}
}

#ifdef HAVE_OPENGL
TEST_CASE("independently encoded lossy HD atlases keep exact frame alpha [display]") {
    glob2test::TempDir scratch("lossy-atlas");
    auto artwork = scratch.path / "data/gfx";
    auto hd = scratch.path / "hd";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    auto fixtures = glob2test::sourceRoot() / "test/fixtures/image-assets";
    std::filesystem::copy_file(fixtures / "native-sheet.webp", artwork / "native-sheet.webp");
    std::ofstream(artwork / "terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (int i = 0; i < 16; ++i) index << "terrain" << i << " 4 2 4 frame.webp -\n";
    index.close();
    for (int i = 0; i < 4; ++i) {
        auto name = "terrain-atlas-mip" + std::to_string(i) + ".webp";
        std::filesystem::copy_file(fixtures / name, hd / name);
    }
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::USEGPU, "lossy HD atlas");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain"));
    REQUIRE(sprite.getFrameCount() == 16);
    REQUIRE(sprite.highResolutionAtlas != nullptr);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(sprite.experimentImages[i] != nullptr);
        CHECK(sprite.getW(i) == 4); CHECK(sprite.getH(i) == 2);
        for (int y = 0; y < lossyAlphaFixture::height; ++y)
            for (int x = 0; x < lossyAlphaFixture::width; ++x) {
                Uint8 r, g, b, a;
                REQUIRE(SDL_ReadSurfacePixel(sprite.highResolutionAtlas->atlas->sdlsurface,
                    (i % 4) * 256 + 64 + x, (i / 4) * 256 + 64 + y, &r, &g, &b, &a));
                CHECK(a == lossyAlphaFixture::alpha[y * lossyAlphaFixture::width + x]);
            }
    }
}
#endif

TEST_CASE("PNG artwork cannot enter the WebP sprite path") {
    SheetFixture fixture;
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    REQUIRE(IMG_SavePNG(surface, (fixture.scratch.path / "legacy0.png").string().c_str()));
    REQUIRE(IMG_SavePNG(surface, (fixture.scratch.path / "disguised0.webp").string().c_str()));
    SDL_DestroySurface(surface);
    GAGCore::Sprite legacy, disguised;
    CHECK_FALSE(legacy.load("legacy"));
    CHECK_FALSE(disguised.load("disguised"));
}

TEST_CASE("Q90 sheet tiles preserve exact frame alpha") {
    SheetFixture fixture;
    using namespace lossyAlphaFixture;
    std::ofstream file(fixture.scratch.path / "lossy.webp", std::ios::binary);
    file.write(reinterpret_cast<const char*>(webp), sizeof(webp));
    file.close();
    fixture.index("lossy", "lossy.webp image 0 2 8 8\nlossy.webp rotated 0 2 8 8\n");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("lossy"));
    REQUIRE(sprite.getFrameCount() == 2);
    for (int frame = 0; frame < 2; ++frame) {
        REQUIRE(sprite.images[frame] != nullptr);
        REQUIRE(sprite.rotated[frame] != nullptr);
        for (auto surface : {sprite.images[frame], sprite.rotated[frame]->orig}) {
            CHECK(surface->getW() == tile); CHECK(surface->getH() == tile);
            for (int y = 0; y < tile; ++y)
                for (int x = 0; x < tile; ++x) {
                    Uint8 r, g, b, a;
                    REQUIRE(SDL_ReadSurfacePixel(surface->sdlsurface, x, y, &r, &g, &b, &a));
                    CHECK(a == alpha[y * width + frame * tile + x]);
                }
        }
    }
}

TEST_CASE("sprites without an index still load one file per frame") {
	SheetFixture fixture;
	writePattern(fixture.scratch.path / "single0.webp", 0, 1, 1, 4);
	writePattern(fixture.scratch.path / "single1r.webp", 1, 1, 1, 4);
	writePattern(fixture.scratch.path / "single3.webp", 3, 1, 1, 4);
	GAGCore::Sprite sprite;
	REQUIRE(sprite.load("single"));
	// Frame 2 is missing, so frame 3 is never reached.
	REQUIRE(sprite.getFrameCount() == 2);
	CHECK(samePixels(sprite.images[0], 0, 4));
	CHECK(sprite.rotated[0] == nullptr);
	CHECK(sprite.images[1] == nullptr);
	CHECK(samePixels(sprite.rotated[1]->orig, 1, 4));
}

TEST_CASE("an index the sheets cannot satisfy falls back to the frames") {
	SheetFixture fixture;
	writePattern(fixture.scratch.path / "broken-sheet-0.webp", 0, 4, 2, 3);
	writePattern(fixture.scratch.path / "broken0.webp", 0, 1, 1, 4);
	const std::pair<const char *, const char *> indexes[] = {
		{"too few tiles", "broken-sheet-0.webp rotated 0 64 3 3\n"},
		{"width not a multiple of the tile", "broken-sheet-0.webp rotated 0 4 4 3\n"},
		{"missing sheet", "absent.webp rotated 0 1 3 3\n"},
		{"frame in two sheets", "broken-sheet-0.webp rotated 0 4 3 3\nbroken-sheet-0.webp rotated 3 1 3 3\n"},
		{"path in the sheet name", "../broken-sheet-0.webp rotated 0 4 3 3\n"},
		{"malformed line", "broken-sheet-0.webp rotated zero 4 3 3\n"},
	};
	for (const auto &[problem, text] : indexes)
	{
		INFO(problem);
		fixture.index("broken", text);
		GAGCore::Sprite sprite;
		REQUIRE(sprite.load("broken"));
		REQUIRE(sprite.getFrameCount() == 1);
		CHECK(samePixels(sprite.images[0], 0, 4));
		CHECK(sprite.rotated[0] == nullptr);
	}
}
}
