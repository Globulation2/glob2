// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingArtwork.h"
#include "BuildingType.h"
#include "Sha256.h"
#include <nlohmann/json.hpp>
#include <webp/decode.h>
#include <map>
#include <algorithm>
#include <set>
#include <vector>
#include <stdexcept>

namespace
{
using Json = nlohmann::json;
[[noreturn]] void invalid(const std::string &message)
{
	throw std::runtime_error("Building artwork: " + message);
}
void fields(const Json &value, std::initializer_list<const char *> allowed)
{
	if (!value.is_object())
		invalid("expected an object");
	for (auto it = value.begin(); it != value.end(); ++it)
		if (std::none_of(allowed.begin(), allowed.end(),
						 [&](const char *key) { return it.key() == key; }))
			invalid("unknown field " + it.key());
}
} // namespace
std::shared_ptr<const BuildingArtwork> BuildingArtwork::decode(std::string bytes,
															   const BuildingsTypes &catalog)
{
	if (bytes.empty())
	{
		for (std::size_t i = 0; i < catalog.size(); ++i)
		{
			const auto &type = *catalog.get(i);
			if (type.gameSprite.starts_with("community/buildings/") ||
				(type.miniSpriteImage >= 0 && type.miniSprite.starts_with("community/buildings/")))
				invalid("required bundle is missing");
		}
		return {};
	}
	if (bytes.size() > MaxBytes || bytes.size() < 16 || bytes.substr(0, 8) != "G2BA0001")
		invalid("invalid bundle size or signature");
	std::size_t at = 8;
	const auto integer = [&]()
	{
		if (at + 4 > bytes.size())
			invalid("truncated integer");
		std::uint32_t n = 0;
		for (unsigned i = 0; i < 4; ++i)
			n |= std::uint32_t(static_cast<unsigned char>(bytes[at++])) << (i * 8);
		return n;
	};
	const auto manifestSize = integer();
	if (!manifestSize || manifestSize > 8u * 1024u * 1024u || manifestSize > bytes.size() - at)
		invalid("invalid manifest size");
	const auto manifestText = bytes.substr(at, manifestSize);
	at += manifestSize;
	const auto manifest = Json::parse(manifestText,
									  [](int depth, Json::parse_event_t, Json &)
									  {
										  if (depth > 64)
											  invalid("manifest nesting exceeds 64 levels");
										  return true;
									  });
	if (!manifest.is_array() || manifest.size() > 4096 || manifest.dump() != manifestText)
		invalid("manifest must use canonical JSON");
	std::map<std::string, std::pair<int, int>> dimensions;
	std::set<std::string> spritePaths;
	std::size_t decodedBytes = 0;
	for (const auto &sprite : manifest)
	{
		fields(sprite, {"key", "frames"});
		const auto key = sprite.at("key").get<std::string>();
		if (key.empty() || key.size() > 64 ||
			!((key.front() >= 'a' && key.front() <= 'z') ||
			  (key.front() >= '0' && key.front() <= '9')) ||
			key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
			invalid("invalid sprite key");
		const auto path = "community/buildings/" + Online::Sha256::hex(sprite.dump()) + "/sprite";
		if (!spritePaths.insert(path).second)
			invalid("duplicate sprite");
		const auto &frames = sprite.at("frames");
		if (!frames.is_array() || frames.empty() || frames.size() > 256)
			invalid("invalid frame count");
		for (const auto &frame : frames)
		{
			fields(frame, {"imageHash", "width", "height", "teamColorHash"});
			if (!frame.at("width").is_number_integer() || !frame.at("height").is_number_integer())
				invalid("invalid dimensions");
			const auto width = frame.at("width").get<std::int64_t>(),
					   height = frame.at("height").get<std::int64_t>();
			if (width < 1 || height < 1 || width > 512 || height > 512)
				invalid("dimensions outside 1..512");
			for (const auto *field : {"imageHash", "teamColorHash"})
				if (frame.contains(field))
				{
					const auto hash = frame.at(field).get<std::string>();
					if (!Online::Sha256::isHexDigest(hash))
						invalid("invalid image hash");
					const std::pair<int, int> size = {int(width), int(height)};
					const auto [old, inserted] = dimensions.emplace(hash, size);
					if (!inserted && old->second != size)
						invalid("conflicting frame dimensions");
					decodedBytes += std::size_t(width) * height * 4;
					if (decodedBytes > 64u * 1024u * 1024u)
						invalid("decoded artwork exceeds 64 MiB");
				}
		}
	}
	const auto count = integer();
	if (count != dimensions.size() || count > 4096)
		invalid("asset count differs from manifest");
	std::map<std::string, std::shared_ptr<const GAGCore::AssetLoader::Bytes>> assets;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (at + 64 > bytes.size())
			invalid("truncated asset hash");
		const auto hash = bytes.substr(at, 64);
		at += 64;
		const auto size = integer();
		if (!size || size > 32u * 1024u * 1024u || size > bytes.size() - at ||
			!dimensions.contains(hash))
			invalid("invalid asset entry");
		const auto image = std::string_view(bytes).substr(at, size);
		at += size;
		if (Online::Sha256::hex(image) != hash)
			invalid("image hash mismatch");
		WebPBitstreamFeatures info;
		if (WebPGetFeatures(reinterpret_cast<const std::uint8_t *>(image.data()), image.size(),
							&info) != VP8_STATUS_OK ||
			info.has_animation || std::pair{info.width, info.height} != dimensions.at(hash))
			invalid("invalid still WebP image or dimensions");
		std::vector<std::uint8_t> pixels(std::size_t(info.width) * info.height * 4);
		if (!WebPDecodeRGBAInto(reinterpret_cast<const std::uint8_t *>(image.data()), image.size(),
								pixels.data(), pixels.size(), info.width * 4))
			invalid("damaged WebP pixels");
		auto content =
			std::make_shared<const GAGCore::AssetLoader::Bytes>(image.begin(), image.end());
		if (!assets.emplace(hash, std::move(content)).second)
			invalid("duplicate asset");
	}
	if (at != bytes.size())
		invalid("trailing bundle bytes");
	auto result = std::make_shared<BuildingArtwork>();
	std::size_t mountedBytes = 0;
	for (const auto &sprite : manifest)
	{
		const auto path = "community/buildings/" + Online::Sha256::hex(sprite.dump()) + "/sprite";
		std::size_t index = 0;
		for (const auto &frame : sprite.at("frames"))
		{
			const auto image = assets.at(frame.at("imageHash").get<std::string>());
			mountedBytes += image->size();
			if (mountedBytes > 64u * 1024u * 1024u)
				invalid("mounted artwork exceeds 64 MiB");
			result->mounted.emplace(path + std::to_string(index) + ".webp", image);
			if (frame.contains("teamColorHash"))
			{
				const auto layer = assets.at(frame.at("teamColorHash").get<std::string>());
				mountedBytes += layer->size();
				if (mountedBytes > 64u * 1024u * 1024u)
					invalid("mounted artwork exceeds 64 MiB");
				result->mounted.emplace(path + std::to_string(index) + "r.webp", layer);
			}
			++index;
		}
	}
	for (std::size_t i = 0; i < catalog.size(); ++i)
	{
		const auto &type = *catalog.get(i);
		const auto check = [&](const std::string &path, int first, int count)
		{
			if (!path.starts_with("community/buildings/"))
				return;
			if (first < 0 || count < 1 || count > 256 || !spritePaths.contains(path))
				invalid("catalog sprite is absent");
			for (int frame = first; frame < first + count; ++frame)
				if (!result->mounted.contains(path + std::to_string(frame) + ".webp"))
					invalid("catalog frame is absent");
		};
		check(type.gameSprite, type.gameSpriteImage,
			  type.crossConnectMultiImage ? 16 : type.gameSpriteCount);
		if (type.miniSpriteImage >= 0)
			check(type.miniSprite, type.miniSpriteImage, 1);
	}
	result->encoded = std::move(bytes);
	return result;
}
