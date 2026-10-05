// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_surface.h>
#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace TerrainVisual
{
class Catalog;
// CPU source pages described by the compiler manifest. Never uploaded whole;
// only composed view pages are subject to the device texture limit.
class CompiledPack
{
  public:
	static std::shared_ptr<CompiledPack> load(const Catalog &);
	// Prepared pixels are valid only for the native artwork actually loaded by
	// Sprite, which may be a user override or an encoded export of the PNG source.
	bool matches(const std::string &source, SDL_Surface *native) const;
	void read(const std::string &, std::vector<std::array<unsigned char, 4>> &);
	std::size_t bytes() const;

  private:
	struct Frame
	{
		unsigned page;
		SDL_Rect rect;
		std::optional<std::uint64_t> nativeFingerprint;
	};
	struct Page
	{
		std::string path;
		int width, height;
		std::shared_ptr<SDL_Surface> pixels;
	};
	std::vector<Page> pages;
	std::map<std::string, Frame> frames;
};
} // namespace TerrainVisual
