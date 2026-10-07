// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapRender.h"
#include "Game.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "render/scene/Scene.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MapRender
{
void validate(const Field& field, int width, int height)
{
	if (width <= 0 || height <= 0 || size_t(width) > MaximumFieldValues / size_t(height)
		|| field.width != width || field.height != height || field.values.size() != size_t(width)*height)
		throw std::invalid_argument("Field dimensions/count do not match the map or exceed the limit");
	if (field.red < 0 || field.red > 255 || field.green < 0 || field.green > 255 || field.blue < 0 || field.blue > 255)
		throw std::invalid_argument("Field colour components must be 0..255");
}
Field readField(const std::string& path)
{
	std::ifstream in(path);
	Field field;
	if (!(in >> field.width >> field.height) || field.width <= 0 || field.height <= 0
		|| size_t(field.width) > MaximumFieldValues / size_t(field.height))
		throw std::invalid_argument("Invalid field header: " + path);
	field.values.resize(size_t(field.width)*field.height);
	for (auto& value : field.values)
		if (!(in >> value)) throw std::invalid_argument("Invalid or missing field value: " + path);
	in >> std::ws;
	if (!in.eof()) throw std::invalid_argument("Trailing field data: " + path);
	return field;
}
unsigned char alpha(std::int64_t value, std::int64_t maximum)
{
	if (value <= 0 || maximum <= 0) return 0;
	// Dividing before multiplying in floating point preserves the entire integer
	// input range without signed overflow. Saturate before the byte conversion.
	return static_cast<unsigned char>(std::min(220.0L, 220.0L * (static_cast<long double>(value) / maximum)));
}
void toPng(const PresentationFrame& scene, const std::string& path, int maximumPixels, const Field* field)
{
	if (maximumPixels <= 0 || maximumPixels > MaximumPixels)
		throw std::invalid_argument("Render limit must be 1..8192 pixels");
	const int w = scene.map.getW(), h = scene.map.getH();
	if (w <= 0 || h <= 0 || size_t(w) > MaximumFieldValues / size_t(h) || scene.entities.teamCount <= 0)
		throw std::invalid_argument("Invalid render scene");
	if (field) validate(*field, w, h);
	const int fullW = w*32, fullH = h*32;
	const int extent = std::max(fullW, fullH);
	const int width = std::max(1, int(std::int64_t(fullW)*std::min(extent, maximumPixels)/extent));
	const int height = std::max(1, int(std::int64_t(fullH)*std::min(extent, maximumPixels)/extent));
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface canvas(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!canvas) throw std::runtime_error(SDL_GetError());
	globalContainer->loadOffscreenGraphics();
	// Captures may predate graphics initialization, or outlive a graphics context.
	// Bind fresh artwork on an export-owned copy, never on immutable sim storage.
	PresentationFrame rendered=scene;
	if (scene.buildingTypes)
	{
		auto types=std::make_shared<std::vector<BuildingType>>(*scene.buildingTypes);
		BuildingsTypes::loadSpritesForTypes(*types);
		const auto remap=[&](const BuildingType* type) -> const BuildingType* {
			return type ? &types->at(type-scene.buildingTypes->data()) : nullptr;
		};
		rendered.entities.typeDefinitions=types;
		rendered.panels.building.type=remap(rendered.panels.building.type);
		rendered.buildingTypes=std::move(types);
	}
	globalContainer->gfx->drawToSurface(canvas.get(), float(std::min(extent, maximumPixels))/extent, [&] {
		Game::ViewState view;
		view.scene = &rendered;
		Game::drawSceneMap(rendered, 0, 0, fullW, fullH, 0, 0, 0, 0, 0, view,
			Game::DRAW_WHOLE_MAP | Game::DRAW_HEALTH_FOOD_BAR | Game::DRAW_BUILDING_RECT,
			nullptr, nullptr, true, 64);
		if (field)
		{
			const auto maximum = *std::max_element(field->values.begin(), field->values.end());
			for (int y=0; y<h; ++y)
				for (int x=0; x<w; ++x)
					globalContainer->gfx->drawFilledRect(x*32, y*32, 32, 32,
						field->red, field->green, field->blue, alpha(field->values[size_t(y)*w+x], maximum));
		}
	});
	const std::filesystem::path destination(path);
	if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
	// Exclusively reserve a sibling directory so concurrent processes cannot
	// truncate each other's temporary PNGs. Rename only a fully closed file.
	std::filesystem::path staging;
	for (unsigned i=0;; ++i)
	{
		staging = path + ".tmp-" + std::to_string(i);
		std::error_code error;
		if (std::filesystem::create_directory(staging, error)) break;
		if (error) throw std::runtime_error("Cannot stage PNG: " + error.message());
		if (i == 1000) throw std::runtime_error("Too many PNG temporary files");
	}
	struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{staging};
	const auto temporary = (staging / "output.png").string();
	SDL_IOStream* out = SDL_IOFromFile(temporary.c_str(), "wb");
	if (!out) throw std::runtime_error(SDL_GetError());
	const bool encoded = IMG_SavePNG_IO(canvas.get(), out, false);
	const bool closed = SDL_CloseIO(out);
	if (!encoded || !closed) throw std::runtime_error("Cannot write PNG: " + path);
#ifdef WIN32
	if (!MoveFileExA(temporary.c_str(), destination.string().c_str(), MOVEFILE_REPLACE_EXISTING))
		throw std::runtime_error("Cannot replace PNG: " + path);
#else
	std::filesystem::rename(temporary, destination);
#endif
}
}
