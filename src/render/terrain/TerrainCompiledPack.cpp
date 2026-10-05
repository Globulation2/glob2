// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCompiledPack.h"
#include "TerrainMaterials.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <charconv>
namespace TerrainVisual
{
namespace
{
std::optional<std::uint64_t> parseFingerprint(const nlohmann::json &frame)
{
	// Old development packs have no attestation. Their sources remain usable.
	if (!frame.contains("native_rgba_fnv1a64"))
		return std::nullopt;
	const auto text = frame.at("native_rgba_fnv1a64").get<std::string>();
	std::uint64_t result = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 16);
	if (text.size() != 16 || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
		throw std::runtime_error("Invalid compiled terrain source fingerprint");
	return result;
}
} // namespace

std::shared_ptr<CompiledPack> CompiledPack::load(const Catalog &catalog)
{
	if (catalog.compiledPack.empty())
		return {};
	std::unique_ptr<std::ifstream> input(
		GAGCore::Toolkit::getFileManager()->openIFStream(catalog.compiledPack));
	if (!input || !*input)
		return {}; // Authoring checkouts use their sprite sources.
	const auto j = nlohmann::json::parse(*input);
	if (j.at("version") != 1)
		throw std::runtime_error("Unsupported compiled terrain pack");
	// An edited catalog can safely use sources until its pack is regenerated.
	if (j.at("catalog").dump() != catalog.serialized)
		return {};
	auto result = std::make_shared<CompiledPack>();
	const auto directory =
		catalog.compiledPack.substr(0, catalog.compiledPack.find_last_of('/') + 1);
	for (const auto &p : j.at("pages"))
	{
		const auto name = p.at("levels").at(0).get<std::string>();
		const int w = p.at("size").at(0), h = p.at("size").at(1);
		if (name.empty() || name.find_first_of("/\\:") != std::string::npos ||
			name.find("..") != std::string::npos || w < 32 || h < 32 || w > 8192 || h > 8192)
			throw std::runtime_error("Invalid compiled terrain page");
		result->pages.push_back({directory + name, w, h, {}});
	}
	for (const auto &f : j.at("frames"))
	{
		const auto source = f.at("source").get<std::string>();
		const unsigned page = f.at("page");
		const auto &r = f.at("rect");
		const SDL_Rect rect{r.at(0), r.at(1), r.at(2), r.at(3)};
		if (page >= result->pages.size() || rect.w != 32 || rect.h != 32 || rect.x < 0 ||
			rect.y < 0 || rect.x > result->pages[page].width - 32 ||
			rect.y > result->pages[page].height - 32 ||
			!result->frames.emplace(source, Frame{page, rect, parseFingerprint(f)}).second)
			throw std::runtime_error("Invalid compiled terrain frame");
	}
	return result;
}
bool CompiledPack::matches(const std::string &source, SDL_Surface *native) const
{
	const auto frame = frames.find(source);
	if (frame == frames.end() || !frame->second.nativeFingerprint || !native || native->w != 32 ||
		native->h != 32)
		return false;
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
		SDL_ConvertSurface(native, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!rgba)
		throw std::bad_alloc();
	// Explicit byte order and fixed-width arithmetic match the asset compiler;
	// pitch padding is excluded. This is a cache identity, not a security hash.
	std::uint64_t fingerprint = 14695981039346656037ull;
	for (int y = 0; y < rgba->h; ++y)
	{
		const auto *row = static_cast<const unsigned char *>(rgba->pixels) + y * rgba->pitch;
		for (int x = 0; x < rgba->w * 4; ++x)
			fingerprint = (fingerprint ^ row[x]) * 1099511628211ull;
	}
	return fingerprint == *frame->second.nativeFingerprint;
}

void CompiledPack::read(const std::string &source,
						std::vector<std::array<unsigned char, 4>> &pixels)
{
	const auto &frame = frames.at(source);
	auto &page = pages[frame.page];
	if (!page.pixels)
	{
		auto *stream = GAGCore::Toolkit::getFileManager()->openImage(page.path.c_str());
		if (!stream)
			throw std::runtime_error("Missing compiled terrain page: " + page.path);
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> loaded(
			IMG_Load_IO(stream, true), SDL_DestroySurface);
		if (!loaded || loaded->w != page.width || loaded->h != page.height)
			throw std::runtime_error("Invalid compiled terrain page dimensions: " + page.path);
		page.pixels = {SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32),
					   SDL_DestroySurface};
		if (!page.pixels)
			throw std::bad_alloc();
	}
	pixels.resize(32 * 32);
	for (int y = 0; y < 32; ++y)
		std::memcpy(pixels.data() + y * 32,
					static_cast<char *>(page.pixels->pixels) +
						(y + frame.rect.y) * page.pixels->pitch + frame.rect.x * 4,
					32 * 4);
}
std::size_t CompiledPack::bytes() const
{
	std::size_t n = 0;
	for (const auto &p : pages)
		if (p.pixels)
			n += p.pixels->pitch * p.pixels->h;
	return n;
}
} // namespace TerrainVisual
