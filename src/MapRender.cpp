// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapRender.h"

#include "Game.h"
#include "GlobalContainer.h"
#include "render/UnitSkin.h"
#include <SDL_image.h>
#include <Toolkit.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <valarray>

#ifndef PRIMARY_FONT
#define PRIMARY_FONT "sans.ttf"
#endif

using namespace GAGCore;

namespace MapRender
{
namespace
{
const int TileSize = 32;
bool assetsReady = false;

void parentDirectory(const std::string &path)
{
	const auto parent = std::filesystem::path(path).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent);
}
} // namespace

void ensureAssets()
{
	if (assetsReady || (globalContainer->gfx && !globalContainer->runNoX))
		return;
	// A headless run has no graphic context and no sprites, and every loader
	// that provides them is gated on runNoX. SDL's dummy driver gives a software
	// context that needs no display and can be read back for saving; the sound
	// and menu half of the normal client startup is deliberately skipped.
	if (!globalContainer->gfx)
	{
		SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
		SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
		globalContainer->gfx = Toolkit::initGraphic(640, 480, 0, "Map render", "glob2");
	}
	if (!globalContainer->standardFont)
	{
		Toolkit::loadFont(std::string("data/fonts/") + PRIMARY_FONT, 13, "standard");
		Toolkit::loadFont(std::string("data/fonts/") + PRIMARY_FONT, 10, "little");
		globalContainer->standardFont = Toolkit::getFont("standard");
		globalContainer->littleFont = Toolkit::getFont("little");
	}
	globalContainer->terrain = Toolkit::getSprite("data/gfx/terrain");
	globalContainer->terrainWater = Toolkit::getSprite("data/gfx/water");
	globalContainer->terrainBlack = Toolkit::getSprite("data/gfx/black");
	globalContainer->terrainShader = Toolkit::getSprite("data/gfx/shade");
	globalContainer->resources = Toolkit::getSprite("data/gfx/ressource");
	globalContainer->resourceMini = Toolkit::getSprite("data/gfx/ressourcemini");
	globalContainer->areaClearing = Toolkit::getSprite("data/gfx/area-clearing");
	globalContainer->areaForbidden = Toolkit::getSprite("data/gfx/area-forbidden");
	globalContainer->areaGuard = Toolkit::getSprite("data/gfx/area-guard");
	globalContainer->bullet = Toolkit::getSprite("data/gfx/bullet");
	globalContainer->bulletExplosion = Toolkit::getSprite("data/gfx/explosion");
	globalContainer->deathAnimation = Toolkit::getSprite("data/gfx/death");
	globalContainer->units = Toolkit::getSprite("data/gfx/unit");
	globalContainer->unitmini = Toolkit::getSprite("data/gfx/unitmini");
	globalContainer->particles = Toolkit::getSprite("data/gfx/particle");
	globalContainer->brush = Toolkit::getSprite("data/gfx/brush");
	globalContainer->magiceffect = Toolkit::getSprite("data/gfx/magiceffect");
	globalContainer->gamegui = Toolkit::getSprite("data/gfx/gamegui");
	// Unit sprites live in the skin registry, not in globalContainer->units, and
	// stay null until this runs: without it every unit draw dereferences null.
	initUnitSkins();
	// Building sprites are attached when the building types load, and that too is
	// skipped headless, so they are attached here for the types already loaded.
	for (std::size_t i = 0; i < globalContainer->buildingsTypes.size(); ++i)
	{
		BuildingType *type = globalContainer->buildingsTypes.get(i);
		if (!type || type->type == "null" || type->gameSpritePtr)
			continue;
		type->gameSpritePtr = Toolkit::getSprite(type->gameSprite.c_str());
		if (type->miniSpriteImage >= 0)
			type->miniSpritePtr = Toolkit::getSprite(type->miniSprite.c_str());
	}
	assetsReady = true;
}

Field readField(const std::string &path)
{
	std::ifstream in(path);
	if (!in)
		throw std::runtime_error("Cannot read field: " + path);
	Field field;
	if (!(in >> field.width >> field.height) || field.width <= 0 || field.height <= 0)
		throw std::runtime_error("Field must start with positive width and height: " + path);
	field.values.reserve(std::size_t(field.width) * field.height);
	int value = 0;
	while (in >> value)
		field.values.push_back(value);
	if (field.values.size() != std::size_t(field.width) * field.height)
		throw std::runtime_error("Field has " + std::to_string(field.values.size()) +
								 " values, expected " +
								 std::to_string(std::size_t(field.width) * field.height) + ": " +
								 path);
	return field;
}

void toPng(Game &game, const std::string &path, int maximumPixels, const Field *field)
{
	ensureAssets();
	const int full = game.map.getW() * TileSize, fullHeight = game.map.getH() * TileSize;
	int width = full, height = fullHeight;
	if (maximumPixels > 0 && std::max(width, height) > maximumPixels)
	{
		const double factor = double(maximumPixels) / std::max(width, height);
		width = std::max(1, int(width * factor));
		height = std::max(1, int(height * factor));
	}
	// Borrow the renderer's drawing interface, not its window or GL context.
	// Software rendering also gives a readable surface when the client uses GL.
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> canvas(
		SDL_CreateRGBSurfaceWithFormat(0, full, fullHeight, 32, SDL_PIXELFORMAT_ARGB8888),
		SDL_FreeSurface);
	if (!canvas)
		throw std::runtime_error("Cannot allocate map surface: " + std::string(SDL_GetError()));
	globalContainer->gfx->drawToSurface(canvas.get(), [&]() {
		Game::ViewState view;
		game.drawMap(0, 0, full, fullHeight, 0, 0, 0, 0, 0, view,
					 Game::DRAW_WHOLE_MAP | Game::DRAW_HEALTH_FOOD_BAR | Game::DRAW_BUILDING_RECT,
					 nullptr, nullptr, true, 0);
		if (field)
		{
			if (field->width != game.map.getW() || field->height != game.map.getH())
				throw std::runtime_error("Field is " + std::to_string(field->width) + "x" +
										 std::to_string(field->height) + " but the map is " +
										 std::to_string(game.map.getW()) + "x" +
										 std::to_string(game.map.getH()));
			int maximum = 0;
			for (int value : field->values)
				maximum = std::max(maximum, value);
			std::valarray<unsigned char> alphas(std::size_t(field->width) * field->height);
			const int scale = std::max(1, maximum);
			for (std::size_t i = 0; i < alphas.size(); ++i)
				alphas[i] = static_cast<unsigned char>(
					std::max(0, std::min(220, 220 * field->values[i] / scale)));
			globalContainer->gfx->drawAlphaMap(alphas, field->width, field->height, 0, 0, TileSize,
											   TileSize, Color(field->red, field->green, field->blue));
		}
	});
	SDL_Surface *rendered = canvas.get();
	parentDirectory(path);
	if (width == full && height == fullHeight)
	{
		if (IMG_SavePNG(rendered, path.c_str()) != 0)
			throw std::runtime_error("Cannot write PNG " + path + ": " + SDL_GetError());
		return;
	}
	// Scale with SDL directly: drawing the context into a GAG surface goes
	// through a path that yielded a blank image, and this needs no knowledge of
	// how the context stores its pixels.
	SDL_Surface *small =
		SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!small)
		throw std::runtime_error("Cannot allocate scaled surface: " + std::string(SDL_GetError()));
	const int scaled = SDL_BlitScaled(rendered, nullptr, small, nullptr);
	const int written = scaled == 0 ? IMG_SavePNG(small, path.c_str()) : -1;
	SDL_FreeSurface(small);
	if (scaled != 0 || written != 0)
		throw std::runtime_error("Cannot write PNG " + path + ": " + SDL_GetError());
}

} // namespace MapRender
