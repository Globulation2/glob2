// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinSpriteManifest.h"
#include "Sha256.h"
#include "SwarmMeshCatalog.h"
#include <nlohmann/json.hpp>
#include <webp/decode.h>
#include <array>
namespace Online
{
bool SkinSpriteManifest::parse(const std::string &bytes, const AuthorizedSkin &skin)
{
	pages.clear();
	try
	{
		if (skin.swarmMesh >= SWARM_MESHES.size() || bytes.size() > 65536 ||
			Sha256::hex(bytes) != skin.spriteManifestHash)
			return false;
		const auto doc = nlohmann::json::parse(bytes);
		if (doc.at("format") != "colony-sprites-v1" ||
			doc.at("renderRevision") != skin.spriteRenderRevision ||
			doc.at("sourceManifestSha256") != (skin.spriteSourceManifestHash.empty()
												   ? skin.manifestHash
												   : skin.spriteSourceManifestHash) ||
			doc.at("textureSha256") != (skin.spriteSourceTextureHash.empty()
											? skin.textureHash
											: skin.spriteSourceTextureHash) ||
			doc.at("materialSha256") != (skin.spriteSourceMaterialHash.empty()
											 ? skin.materialHash
											 : skin.spriteSourceMaterialHash) ||
			doc.at("swarmMesh") != SWARM_MESHES[skin.swarmMesh].id ||
			doc.at("swarmViewAngle") != skin.swarmViewAngle || doc.at("tileSize") != 128 ||
			doc.at("padding") != 1.25 ||
			doc.at("frameMapping") !=
				nlohmann::json{
					{"directions", 8}, {"phases", 32}, {"phaseShift", 3}, {"direction8Shift", 5}} ||
			doc.at("logicalSizes") != SkinSpriteLogicalSizes ||
			doc.at("encoding") != "bundled-images-v3-webp-only")
			return false;
		const auto &records = doc.at("pages");
		if (!records.is_array() || records.size() != 29)
			return false;
		std::vector<SkinSpritePage> candidate;
		for (unsigned i = 0; i < records.size(); ++i)
		{
			const auto &r = records[i];
			SkinSpritePage p;
			p.clip = i < 28 ? i / 4 : 7;
			p.first = i < 28 ? (i % 4) * 64 : 0;
			p.frames = i < 28 ? 64 : 1;
			p.size = i < 28 ? 1024 : 128;
			p.hash = r.at("sha256").get<std::string>();
			if (!r.at("bytes").is_number_unsigned() && !r.at("bytes").is_number_integer())
				return false;
			if (r.at("bytes") < 1 || r.at("bytes") > 2 * 1024 * 1024)
				return false;
			p.bytes = r.at("bytes").get<unsigned>();
			if (!Sha256::isHexDigest(p.hash) || r.at("clip") != SkinSpriteClips[p.clip] ||
				r.at("first") != p.first || r.at("frames") != p.frames || r.at("width") != p.size ||
				r.at("height") != p.size)
				return false;
			candidate.push_back(p);
		}
		pages = std::move(candidate);
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
bool validSkinSpritePage(const std::string &bytes, const SkinSpritePage &page)
{
	if (bytes.size() != page.bytes || bytes.size() > 2 * 1024 * 1024 ||
		Sha256::hex(bytes) != page.hash)
		return false;
	WebPBitstreamFeatures info{};
	return WebPGetFeatures(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), &info) ==
			   VP8_STATUS_OK &&
		   !info.has_animation && (info.format == 1 || info.format == 2) &&
		   info.width == int(page.size) && info.height == int(page.size);
}
} // namespace Online
