// SPDX-License-Identifier: GPL-3.0-or-later
// GAGCore::Sprite's sheet loader (the <name>.sheet index tools/package_assets.py
// writes for packaged builds) and its fallback to one file per frame.
#include "Glob2Test.h"
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
	const int rows = (count + columns - 1) / columns;
	SDL_Surface *sheet = SDL_CreateSurface(columns * tile, rows * tile, SDL_PIXELFORMAT_RGBA32);
	REQUIRE(sheet != nullptr);
	SDL_ClearSurface(sheet, 0, 0, 0, 0);
	for (int i = 0; i < count; ++i)
		for (int y = 0; y < tile; ++y)
			for (int x = 0; x < tile; ++x)
			{
				const SDL_Color c = patternPixel(first + i, x, y);
				REQUIRE(SDL_WriteSurfacePixel(sheet, (i % columns) * tile + x, (i / columns) * tile + y,
					c.r, c.g, c.b, c.a));
			}
	REQUIRE(IMG_SavePNG(sheet, path.string().c_str()));
	SDL_DestroySurface(sheet);
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
		std::ofstream(scratch.path / (name + ".sheet")) << text;
	}
};
}

TEST_SUITE("SpriteSheets") {
TEST_CASE("sheet tiles load as the frames they were packed from") {
	SheetFixture fixture;
	// Five recolorable 3x3 frames, sixteen to a row in the packer but two here,
	// and two plain 4x4 frames starting at frame 2 in a wider grid.
	writePattern(fixture.scratch.path / "set-sheet-0.png", 0, 5, 2, 3);
	writePattern(fixture.scratch.path / "set-sheet-1.png", 2, 2, 4, 4);
	fixture.index("set", "# comment\n\nset-sheet-0.png rotated 0 5 3 3\nset-sheet-1.png image 2 2 4 4\n");
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

TEST_CASE("sprites without an index still load one file per frame") {
	SheetFixture fixture;
	writePattern(fixture.scratch.path / "single0.png", 0, 1, 1, 4);
	writePattern(fixture.scratch.path / "single1r.png", 1, 1, 1, 4);
	writePattern(fixture.scratch.path / "single3.png", 3, 1, 1, 4);
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
	writePattern(fixture.scratch.path / "broken-sheet-0.png", 0, 4, 2, 3);
	writePattern(fixture.scratch.path / "broken0.png", 0, 1, 1, 4);
	const std::pair<const char *, const char *> indexes[] = {
		{"too few tiles", "broken-sheet-0.png rotated 0 64 3 3\n"},
		{"width not a multiple of the tile", "broken-sheet-0.png rotated 0 4 4 3\n"},
		{"missing sheet", "absent.png rotated 0 1 3 3\n"},
		{"frame in two sheets", "broken-sheet-0.png rotated 0 4 3 3\nbroken-sheet-0.png rotated 3 1 3 3\n"},
		{"path in the sheet name", "../broken-sheet-0.png rotated 0 4 3 3\n"},
		{"malformed line", "broken-sheet-0.png rotated zero 4 3 3\n"},
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
