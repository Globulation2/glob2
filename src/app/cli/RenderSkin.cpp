// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
#include "SkinRenderRecipe.h"
#else
#define SKIN_RENDER_REVISION "unavailable"
#define SKIN_WEBP_VERSION "1.6.0"
#endif
#include <algorithm>
#include "online/Sha256.h"
#include "online/SwarmMeshCatalog.h"
#include "online/SkinSpriteManifest.h"
#include "online/SkinViewTransforms.h"
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SkinMesh.h>
#include <glob2/SkinMaterials.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#if defined(HAVE_OPENGL) && !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
#include <webp/encode.h>
#include <webp/decode.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#endif
namespace
{
using Json = nlohmann::json;
std::string read(const std::string &path, std::size_t limit)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in || in.tellg() < 0 || std::size_t(in.tellg()) > limit)
		throw std::runtime_error("missing or oversized input: " + path);
	std::string bytes(std::size_t(in.tellg()), '\0');
	in.seekg(0);
	in.read(bytes.data(), bytes.size());
	if (!in)
		throw std::runtime_error("cannot read input");
	return bytes;
}
void write(const std::filesystem::path &path, const std::string &bytes)
{
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), bytes.size());
	if (!out)
		throw std::runtime_error("cannot write output: " + path.string());
}
#if defined(HAVE_OPENGL) && !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
void checkEncoderVersion()
{
	if (WebPGetEncoderVersion() != SKIN_WEBP_ENCODER_VERSION)
		throw std::runtime_error("skin exports require pinned libwebp " SKIN_WEBP_VERSION);
}
// Bound decoded allocations before SDL_image sees either permitted input format.
void checkImageHeader(const std::string &bytes)
{
	unsigned width = 0, height = 0;
	if (bytes.size() >= 33 && std::memcmp(bytes.data(), "\211PNG\r\n\032\n", 8) == 0 &&
		bytes.substr(12, 4) == "IHDR")
	{
		const auto number = [&](unsigned at)
		{
			unsigned n = 0;
			for (unsigned i = 0; i < 4; ++i)
				n = (n << 8) | static_cast<unsigned char>(bytes[at + i]);
			return n;
		};
		width = number(16);
		height = number(20);
		if (static_cast<unsigned char>(bytes[24]) != 8)
			throw std::runtime_error("skin inputs require 8-bit pixels");
	}
	else
	{
		WebPBitstreamFeatures info{};
		if (WebPGetFeatures(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), &info) !=
				VP8_STATUS_OK ||
			info.has_animation)
			throw std::runtime_error("skin inputs must be static PNG or WebP images");
		width = info.width;
		height = info.height;
	}
	if (width != 512 || height != 512)
		throw std::runtime_error("skin inputs must be 512x512");
}
std::unique_ptr<GAGCore::DrawableSurface> loadInput(const std::string &bytes, bool material)
{
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> loaded(
		IMG_Load_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true), SDL_DestroySurface);
	if (!loaded || loaded->w != 512 || loaded->h != 512)
		throw std::runtime_error("invalid skin image");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
		SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!rgba || !SDL_LockSurface(rgba.get()))
		throw std::runtime_error("cannot validate skin pixels");
	bool valid = true;
	for (int y = 0; y < 512 && valid; ++y)
		for (int x = 0; x < 512; ++x)
		{
			const auto *p = static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch + x * 4;
			if (p[3] != 255 || (material && (p[0] >= SKIN_MATERIAL_COUNT || p[0] != p[1] || p[0] != p[2])))
			{
				valid = false;
				break;
			}
		}
	SDL_UnlockSurface(rgba.get());
	if (!valid)
		throw std::runtime_error(material ? "invalid material ids or alpha"
										  : "skin paint must be opaque");
	// Offline inputs may be immutable PNG sources. The game's asset loader
	// accepts only WebP, so adopt these already bounded and validated pixels.
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
		SDL_ConvertSurface(rgba.get(), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
	if (!pixels)
		throw std::runtime_error(SDL_GetError());
	auto result = std::make_unique<GAGCore::DrawableSurface>(
		pixels.get(), GAGCore::DrawableSurface::AdoptPixels{});
	pixels.release();
	return result;
}
std::string encodeCandidate(const std::vector<uint8_t> &rgba, unsigned size, bool lossless)
{
	WebPConfig config;
	if (!WebPConfigInit(&config))
		throw std::runtime_error("WebP encoder unavailable");
	config.lossless = lossless;
	config.quality = lossless ? SKIN_WEBP_LOSSLESS_QUALITY : SKIN_WEBP_QUALITY;
	config.method = lossless ? SKIN_WEBP_LOSSLESS_METHOD : SKIN_WEBP_METHOD;
	config.exact = 1;
	config.alpha_quality = 100;
	WebPPicture picture;
	if (!WebPPictureInit(&picture))
		throw std::runtime_error("WebP picture unavailable");
	struct Free
	{
		WebPPicture *p;
		~Free() { WebPPictureFree(p); }
	} free{&picture};
	picture.use_argb = 1;
	picture.width = size;
	picture.height = size;
	WebPMemoryWriter writer;
	WebPMemoryWriterInit(&writer);
	struct Clear
	{
		WebPMemoryWriter *p;
		~Clear() { WebPMemoryWriterClear(p); }
	} clear{&writer};
	picture.writer = WebPMemoryWrite;
	picture.custom_ptr = &writer;
	if (!WebPPictureImportRGBA(&picture, rgba.data(), size * 4) || !WebPEncode(&config, &picture))
		throw std::runtime_error("WebP encoding failed");
	std::string bytes(reinterpret_cast<char *>(writer.mem), writer.size);
	std::vector<uint8_t> decoded(rgba.size());
	if (!WebPDecodeRGBAInto(writer.mem, writer.size, decoded.data(), decoded.size(), size * 4))
		throw std::runtime_error("WebP verification failed");
	if (lossless && decoded != rgba)
		throw std::runtime_error("lossless WebP changed pixels");
	for (std::size_t i = 3; i < rgba.size(); i += 4)
		if (rgba[i] != decoded[i])
			throw std::runtime_error("WebP changed alpha");
	return bytes;
}
std::string encode(const std::vector<uint8_t> &rgba, unsigned size)
{
	const auto lossy = encodeCandidate(rgba, size, false),
			   lossless = encodeCandidate(rgba, size, true);
	const auto &selected = lossless.size() < lossy.size() ? lossless : lossy;
	if (selected.size() > 2 * 1024 * 1024)
		throw std::runtime_error("sprite page exceeds delivery limit");
	return selected;
}
#endif
} // namespace
int runRenderSkin(int argc, char **argv)
{
	if (argc < 2)
		return -1;
	const std::string command(argv[1]);
	if (command != "--render-skin" && command != "--skin-render-info")
		return -1;
	std::filesystem::path staging;
	try
	{
		if (command == "--skin-render-info")
		{
			if (argc != 2)
				throw std::runtime_error("usage: glob2 --skin-render-info");
#if defined(HAVE_OPENGL) && !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
			// Reject mismatched runtime dependencies before the worker registers
			// a revision and consumes publication jobs.
			checkEncoderVersion();
			std::cout << Json{{"format", "colony-sprites-v1"},
							  {"renderRevision", SKIN_RENDER_REVISION},
							  {"encoding", "bundled-images-v3-webp-only"},
							  {"webpVersion", SKIN_WEBP_VERSION}}
							 .dump()
					  << '\n';
			return 0;
#else
			throw std::runtime_error("requires a native OpenGL client build");
#endif
		}
		std::map<std::string, std::string> options;
		for (int i = 2; i < argc; i += 2)
		{
			const std::string key(argv[i]);
			if (i + 1 >= argc ||
				(key != "--manifest" && key != "--texture" && key != "--material" &&
				 key != "--output-dir") ||
				!options.emplace(key, argv[i + 1]).second)
				throw std::runtime_error("usage: glob2 --render-skin --manifest JSON --texture "
										 "IMAGE --material IMAGE --output-dir DIRECTORY");
		}
		if (options.size() != 4)
			throw std::runtime_error("all four input/output options are required");
		const auto source = Json::parse(read(options.at("--manifest"), 65536));
		const auto texture = read(options.at("--texture"), 1024 * 1024),
				   material = read(options.at("--material"), 256 * 1024);
		const auto textureHash = Online::Sha256::hex(texture),
				   materialHash = Online::Sha256::hex(material);
		const auto color = source.at("buildingColor");
		const auto angle = source.value("swarmViewAngle", Json(0));
		const auto meshId = source.value("swarmMesh", std::string("classic"));
		const int choice = Online::swarmMeshIndex(meshId);
		if (source.at("layout") != "colony-v2" || source.at("textureSha256") != textureHash ||
			source.at("materialSha256") != materialHash || !color.is_number_integer() ||
			color < 0 || color > 0xffffff || !angle.is_number_integer() || angle < 0 ||
			angle > 359 || choice < 0)
			throw std::runtime_error("invalid skin content");
		nlohmann::ordered_json identity = {{"skinId", source.at("skinId")},
										   {"textureSha256", textureHash},
										   {"materialSha256", materialHash},
										   {"layout", "colony-v2"},
										   {"buildingColor", color}};
		if (choice)
			identity["swarmMesh"] = meshId;
		if (angle != 0)
			identity["swarmViewAngle"] = angle;
		const auto id = source.at("skinId").get<std::string>();
		if (id.size() != 36 || id[8] != '-' || id[13] != '-' || id[18] != '-' || id[23] != '-' ||
			std::count(id.begin(), id.end(), '-') != 4 ||
			id.find_first_not_of("0123456789abcdef-") != std::string::npos)
			throw std::runtime_error("invalid skin identity");
		const auto sourceHash = Online::Sha256::hex(identity.dump());
		if (source.at("manifestSha256") != sourceHash)
			throw std::runtime_error("skin manifest hash mismatch");
#if defined(HAVE_OPENGL) && !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
		checkEncoderVersion();
		checkImageHeader(texture);
		checkImageHeader(material);
		const std::filesystem::path output = options.at("--output-dir");
		if (std::filesystem::exists(output))
			throw std::runtime_error("output directory already exists");
		GAGCore::Toolkit::init("glob2-skin-render");
		struct Close
		{
			~Close() { GAGCore::Toolkit::close(); }
		} close;
		auto *gfx = GAGCore::Toolkit::initGraphic(
			128, 128, GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::NOAUDIO,
			"Skin export");
		auto paint = loadInput(texture, false);
		auto ids = loadInput(material, true);
		const std::filesystem::path candidate = output.string() + ".partial";
		if (!std::filesystem::create_directory(candidate))
			throw std::runtime_error("output staging directory already exists");
		staging = candidate;
		Json result = {
			{"format", "colony-sprites-v1"},
			{"renderRevision", SKIN_RENDER_REVISION},
			{"sourceManifestSha256", sourceHash},
			{"textureSha256", textureHash},
			{"materialSha256", materialHash},
			{"swarmMesh", meshId},
			{"swarmViewAngle", angle},
			{"frameMapping",
			 {{"directions", 8}, {"phases", 32}, {"phaseShift", 3}, {"direction8Shift", 5}}},
			{"tileSize", 128},
			{"padding", 1.25},
			{"logicalSizes", Online::SkinSpriteLogicalSizes},
			{"encoding", "bundled-images-v3-webp-only"},
			{"pages", Json::array()}};
		for (unsigned clip = 0; clip < 8; ++clip)
		{
			GAGCore::SkinMesh mesh;
			std::string error;
			// Published sprites always render the rigs: the recipe digest covers
			// their bytes and decoders, so GLOB2_SKIN_RIGS is deliberately ignored.
			const std::string file = clip < 7 ? GAGCore::skinClipFile(Online::SkinSpriteClips[clip])
											  : std::string(Online::SWARM_MESHES[choice].file);
			if (!mesh.load("data/skins/colony-v1/" + file, error))
				throw std::runtime_error(file + ": " + error);
			if (clip == 7 && angle != 0)
			{
				const auto &view = Online::SkinViews[choice];
				mesh = mesh.rotatedView(angle.get<unsigned>(), view.inverse, view.projection,
										view.normals);
			}
			if (mesh.frames != (clip < 7 ? 256u : 1u) ||
				mesh.logicalSize != Online::SkinSpriteLogicalSizes[clip])
				throw std::runtime_error("unsupported mesh dimensions");
			const unsigned region = clip < 3 ? 0 : clip < 6 ? 1 : clip == 6 ? 2 : 3;
			for (unsigned page = 0; page < (clip < 7 ? 4u : 1u); ++page)
			{
				const unsigned size = clip < 7 ? 1024 : 128, frames = clip < 7 ? 64 : 1;
				std::vector<uint8_t> pixels(size * size * 4, 0), tile;
				for (unsigned frame = 0; frame < frames; ++frame)
				{
					if (!gfx->readSkinMesh(
							{&mesh, page * 64 + frame, paint.get(), ids.get(), uint8_t(region)},
							tile))
						throw std::runtime_error("OpenGL skin rendering unavailable");
					const unsigned col = frame % 8, row = frame / 8;
					for (unsigned y = 0; y < 128; ++y)
						std::copy_n(tile.data() + y * 128 * 4, 128 * 4,
									pixels.data() + ((row * 128 + y) * size + col * 128) * 4);
				}
				const auto bytes = encode(pixels, size), hash = Online::Sha256::hex(bytes);
				write(staging / (hash + ".webp"), bytes);
				result["pages"].push_back({{"clip", Online::SkinSpriteClips[clip]},
										   {"first", page * 64},
										   {"frames", frames},
										   {"width", size},
										   {"height", size},
										   {"sha256", hash},
										   {"bytes", bytes.size()}});
			}
		}
		write(staging / "manifest.json", result.dump());
		std::filesystem::rename(staging, output);
		staging.clear();
		std::cout << result.dump() << '\n';
		return 0;
#else
		throw std::runtime_error("skin export requires a native OpenGL client build");
#endif
	}
	catch (const std::exception &e)
	{
		if (!staging.empty())
		{
			std::error_code error;
			std::filesystem::remove_all(staging, error);
		}
		std::cerr << command.substr(2) << ": " << e.what() << '\n';
		return 1;
	}
}
