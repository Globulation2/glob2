// SPDX-License-Identifier: GPL-3.0-or-later
#include <Toolkit.h>
#include <GraphicContext.h>
#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <cassert>
#include <iostream>
#include <vector>
#include "render/UnitAnimation.h"
int main()
{
	using namespace GAGCore;
	Toolkit::init("codex-glob2-cache-gpu-check");
	auto *g = Toolkit::initGraphic(160, 100, GraphicContext::USEGPU, "Cache pixel comparison");
	auto *s = Toolkit::getSprite("data/gfx/unit");
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	std::vector<Uint8> a(viewport[2] * viewport[3] * 4), b(a.size());
	double total = 0;
	int max = 0;
	size_t n = 0;
	for (int base = 0; base <= 384; base += 64)
		for (int dir = 0; dir < 8; ++dir)
			for (int delta : {0, 7, 64, 255})
				for (int color = 0; color < 3; ++color)
				{
					s->setBaseColor(color == 0   ? Color(255, 60, 40)
					                : color == 1 ? Color(0, 255, 128)
					                             : Color(70, 110, 255));
					std::vector<std::pair<int, int>> frames;
					drawUnitMotionBlur(base, dir, delta, 30,
					                   [&](int f, int alpha) { frames.emplace_back(f, alpha); });
					g->drawFilledRect(0, 0, 160, 100, 30, 90, 45);
					for (auto f : frames)
						g->drawSprite(0, 0, s, f.first, f.second);
					glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
					             GL_UNSIGNED_BYTE, a.data());
					g->drawFilledRect(0, 0, 160, 100, 30, 90, 45);
					g->drawSurface(0, 0, s->getCachedComposite(frames));
					glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
					             GL_UNSIGNED_BYTE, b.data());
					for (size_t i = 0; i < a.size(); ++i)
						if (i % 4 != 3)
						{
							int d = abs(a[i] - b[i]);
							total += d;
							max = std::max(max, d);
							++n;
						}
				}
	std::cout << "GPU RGB comparison mean=" << total / n << " max=" << max << std::endl;
	assert(max <= 5);
	Toolkit::close();
}
