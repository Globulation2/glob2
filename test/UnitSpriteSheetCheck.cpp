// SPDX-License-Identifier: GPL-3.0-or-later
// Checks GAGCore::Sprite's sheet loader: a synthetic index whose tiles carry
// known pixels, the fallback to the per-frame layout, and the shipped
// data/gfx/unit sheets. Run from the repository root.
#include <Toolkit.h>
#include <FileManager.h>
#include <GraphicContext.h>
#include <SDL_image.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace GAGCore;

struct InspectSprite : Sprite
{
	bool hasImage(int index) { return images[index] != NULL; }
	bool hasRotated(int index) { return rotated[index] != NULL; }
	//! Pixel of a plain frame, read back through the surface's own format
	void imagePixel(int index, int x, int y, Uint8 *rgba)
	{
		SDL_Surface *surface = images[index]->getSDLSurface();
		Uint32 pixel = *reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels)
			+ y * surface->pitch + x * surface->format->BytesPerPixel);
		SDL_GetRGBA(pixel, surface->format, rgba, rgba + 1, rgba + 2, rgba + 3);
	}
};

// Frame i of the synthetic sheets, an arbitrary but position-dependent pattern.
static void patternPixel(int frame, int x, int y, Uint8 *rgba)
{
	rgba[0] = static_cast<Uint8>(frame * 10);
	rgba[1] = static_cast<Uint8>(x * 20);
	rgba[2] = static_cast<Uint8>(y * 30);
	rgba[3] = static_cast<Uint8>(255 - frame);
}

static void writePattern(const std::string &path, int first, int count, int columns, int tile)
{
	const int rows = (count + columns - 1) / columns;
	SDL_Surface *sheet = SDL_CreateRGBSurfaceWithFormat(0, columns * tile, rows * tile, 32,
		SDL_PIXELFORMAT_RGBA32);
	assert(sheet);
	for (int i = 0; i < count; ++i)
		for (int y = 0; y < tile; ++y)
			for (int x = 0; x < tile; ++x)
			{
				Uint8 rgba[4];
				patternPixel(first + i, x, y, rgba);
				*reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(sheet->pixels)
					+ ((i / columns) * tile + y) * sheet->pitch
					+ ((i % columns) * tile + x) * 4) =
					SDL_MapRGBA(sheet->format, rgba[0], rgba[1], rgba[2], rgba[3]);
			}
	assert(IMG_SavePNG(sheet, path.c_str()) == 0);
	SDL_FreeSurface(sheet);
}

int main()
{
	Toolkit::init("glob2-sprite-sheet-check");
	Toolkit::initGraphic(100, 100, 0, "Sprite sheet validation");

	const std::filesystem::path root = std::filesystem::temp_directory_path() / "glob2-sheet-check";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "sheets");
	std::filesystem::create_directories(root / "frames");
	Toolkit::getFileManager()->addDir(root.string());

	// Two sheets of different tile sizes: five recolorable frames from the
	// first, two plain frames starting at frame 2 from the second.
	writePattern((root / "sheets/pattern-r.png").string(), 0, 5, 2, 3);
	writePattern((root / "sheets/pattern.png").string(), 2, 2, 4, 4);
	std::ofstream index((root / "sheets/set.sheet").string());
	index << "# synthetic index\n"
	      << "\n"
	      << "pattern-r.png rotated 0 5 3 3\n"
	      << "pattern.png image 2 2 4 4\n";
	index.close();

	{
		InspectSprite sprite;
		assert(sprite.load("sheets/set"));
		assert(sprite.getFrameCount() == 5);
		for (int i = 0; i < 5; ++i)
		{
			assert(sprite.hasRotated(i));
			assert(sprite.hasImage(i) == (i == 2 || i == 3));
			// getW/getH fall back to the recolorable layer where there is no
			// plain one, so they expose both sheets' tile sizes.
			assert(sprite.getW(i) == (i == 2 || i == 3 ? 4 : 3));
			assert(sprite.getH(i) == (i == 2 || i == 3 ? 4 : 3));
		}
		// Every plain tile must carry exactly the pixels it was packed with.
		for (int i = 2; i <= 3; ++i)
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					Uint8 expected[4], actual[4];
					patternPixel(i, x, y, expected);
					sprite.imagePixel(i, x, y, actual);
					assert(memcmp(expected, actual, 4) == 0);
				}
	}

	// No index: the per-frame layout still loads, and its frames stop at the
	// first missing pair.
	writePattern((root / "frames/single0.png").string(), 0, 1, 1, 4);
	writePattern((root / "frames/single2.png").string(), 2, 1, 1, 4);
	{
		InspectSprite sprite;
		assert(sprite.load("frames/single"));
		assert(sprite.getFrameCount() == 1);
		assert(sprite.hasImage(0) && !sprite.hasRotated(0));
	}

	// A sheet the index cannot be cut from is rejected, not half-loaded.
	std::filesystem::copy_file(root / "sheets/pattern-r.png", root / "frames/broken0.png");
	std::ofstream broken((root / "frames/broken.sheet").string());
	broken << "broken0.png rotated 0 64 3 3\n";
	broken.close();
	{
		InspectSprite sprite;
		assert(sprite.load("frames/broken"));
		assert(sprite.getFrameCount() == 1);
		assert(sprite.hasImage(0) && !sprite.hasRotated(0));
	}

	// The shipped unit set: 1,792 recolorable frames, shadows only on the four
	// sets that render one, and each set's native tile size.
	{
		InspectSprite sprite;
		assert(sprite.load("data/gfx/unit"));
		assert(sprite.getFrameCount() == 1792);
		const int sizes[7] = {32, 38, 38, 38, 40, 40, 40};
		const bool shadow[7] = {false, true, false, true, true, false, true};
		for (int set = 0; set < 7; ++set)
			for (int i = 0; i < 256; ++i)
			{
				const int frame = set * 256 + i;
				assert(sprite.hasRotated(frame));
				assert(sprite.hasImage(frame) == shadow[set]);
				assert(sprite.getW(frame) == sizes[set]);
				assert(sprite.getH(frame) == sizes[set]);
			}
	}

	std::filesystem::remove_all(root);
	std::cout << "Sprite sheet check passed" << std::endl;
	return 0;
}
