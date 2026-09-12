// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "GlobalContainer.h"
#include "NewMapScreen.h"
#include "CustomGameSetup.h"
#include <GUIList.h>
#include <GUINumber.h>
#include <Toolkit.h>
#include <SDL_image.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <set>

GlobalContainer *globalContainer = nullptr;
using D = MapGenerationDescriptor;

class MapGeneratorDefaultsTest
{
  public:
	static void select(NewMapScreen &s, D::Method method)
	{
		s.methods->setSelectionIndex(method);
		s.onAction(s.methods, GAGGUI::LIST_ELEMENT_SELECTED, method, 0);
	}
	static void sameControls(const D &a, const D &b)
	{
		assert(a.method == b.method);
		for (const auto &c : D::controls(a.method))
			assert(c.get(a) == c.get(b));
		for (const auto &c : D::sharedControls())
			assert(c.get(a) == c.get(b));
	}
	static void edit(NewMapScreen &s, const char *label, int value)
	{
		for (auto &w : s.controlWidgets)
			if (w.number->visible && std::strcmp(w.definition.label, label) == 0)
			{
				w.number->setNth((value - w.definition.minimum) / w.definition.step);
				s.onAction(w.number, GAGGUI::NUMBER_ELEMENT_SELECTED, 0, 0);
				assert(w.definition.get(s.descriptor) == value);
				return;
			}
		assert(false);
	}
	static void run(const char *output)
	{
		NewMapScreen s;
		s.gfx = globalContainer->gfx;
		s.dispatchInit();
		CustomGameSetup lobby;
		D river;
		river.setMethodDefaults(D::eRIVER);
		sameControls(lobby.generator, river);
		for (int m = 0; m <= D::eOLDISLANDS; ++m)
		{
			auto method = static_cast<D::Method>(m);
			D expected;
			expected.setMethodDefaults(method);
			select(s, method);
			lobby.generatorHistory.select(lobby.generator, method);
			sameControls(s.descriptor, expected);
			sameControls(lobby.generator, expected);
			s.dispatchTimer(1);
			sameControls(s.descriptor, expected);
			std::set<std::string> labels;
			for (const auto &c : D::controls(method))
			{
				assert(labels.insert(c.label).second);
				assert(c.step > 0 && c.maximum >= c.minimum);
				assert((c.maximum - c.minimum) % c.step == 0);
				assert(c.get(expected) == c.defaultValue);
				assert(c.normalize(c.defaultValue) == c.defaultValue);
				assert(c.normalize(c.minimum - 100) == c.minimum);
				assert(c.normalize(c.maximum + 100) == c.maximum);
				// The actual editor must expose every value, including 75 grass and 65 size.
				for (int v = c.minimum; v <= c.maximum; v += c.step)
					edit(s, c.label, v);
				edit(s, c.label, c.defaultValue);
			}
			sameControls(s.descriptor, expected);
			D decoded;
			assert(decoded.setData(s.descriptor.getData(), s.descriptor.getDataLength()));
			sameControls(decoded, expected);
			if (output && m >= 4 && m <= 8)
			{
				s.gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(34, 55, 42));
				for (auto *w : s.widgets)
					if (w->visible)
						w->paint();
				std::string path = std::string(output) + "/editor-" + std::to_string(m) + ".png";
				assert(IMG_SavePNG(s.gfx->getSDLSurface(), path.c_str()) == 0);
			}
		}
		select(s, D::eRIVER);
		edit(s, "Water weight", 37);
		edit(s, "Width", 8);
		edit(s, "Colonies", 6);
		lobby.generatorHistory.select(lobby.generator, D::eRIVER);
		lobby.generator.waterRatio = 37;
		lobby.generator.wDec = 8;
		lobby.generator.nbTeams = 6;
		for (auto method : {D::eISLANDS, D::eISLES, D::eRIVER})
		{
			select(s, method);
			lobby.generatorHistory.select(lobby.generator, method);
			sameControls(s.descriptor, lobby.generator);
			assert(s.descriptor.wDec == 8 && s.descriptor.nbTeams == 6);
		}
		assert(s.descriptor.waterRatio == 37);
		lobby.random = true;
		lobby.generator.waterRatio = lobby.generator.grassRatio = lobby.generator.sandRatio =
			lobby.generator.desertRatio = 0;
		assert(!lobby.validation().empty());
		lobby.generator.grassRatio = 75;
		assert(lobby.validation().empty());
		puts("PASS shared presets, ranges and steps; all editor values; lobby/editor mode memory; "
			 "serialization and validation");
	}
};
int main(int argc, char **argv)
{
	assert(argc == 2 || argc == 3);
	SDL_SetMainReady();
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.load();
	globals.gfx = GAGCore::Toolkit::initGraphic(640, 480, 0, "Map defaults test", "");
	GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 20, "menu");
	GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 13, "standard");
	MapGeneratorDefaultsTest::run(argc == 3 ? argv[2] : nullptr);
}
