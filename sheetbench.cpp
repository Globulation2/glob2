// Times GAGCore::Sprite::load("data/gfx/unit") from a data root; also dumps
// every loaded frame's RGBA to compare packed and per-frame loads.
#include <Toolkit.h>
#include <FileManager.h>
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
using namespace GAGCore;
static void dump(std::ofstream &out, DrawableSurface *s)
{
	if (!s) { out.put(0); return; }
	out.put(1);
	int w = s->getW(), h = s->getH();
	out.write((char*)&w, 4); out.write((char*)&h, 4);
	for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
	{ Uint8 p[4]; SDL_ReadSurfacePixel(s->sdlsurface, x, y, p, p+1, p+2, p+3); out.write((char*)p, 4); }
}
int main(int argc, char **argv)
{
	Toolkit::init("glob2-sheet-bench");
	Toolkit::initGraphic(64, 64, 0, "bench");
	Toolkit::getFileManager()->dirList.clear();
	Toolkit::getFileManager()->addDir(argv[1]);
	int runs = argc > 2 ? atoi(argv[2]) : 5;
	for (int r = 0; r < runs; ++r)
	{
		auto t0 = std::chrono::steady_clock::now();
		Sprite *sprite = new Sprite;
		bool ok = sprite->load("data/gfx/unit");
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		printf("run %d ok=%d frames=%d load_ms=%.1f\n", r, ok, sprite->getFrameCount(), ms);
		if (r == 0 && argc > 3)
		{
			std::ofstream out(argv[3], std::ios::binary);
			for (int i = 0; i < sprite->getFrameCount(); ++i)
			{ dump(out, sprite->images[i]); dump(out, sprite->rotated[i] ? sprite->rotated[i]->orig : nullptr); }
		}
		delete sprite;
	}
	return 0;
}
