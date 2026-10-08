// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <AssetLoader.h>
#include <memory>
#include <string>

class BuildingsTypes;
// Portable content-addressed image bundle. Decode before exposing any sprite paths.
// Wire layout: G2BA0001, LE uint32 canonical descriptor JSON length, JSON bytes,
// LE uint32 asset count, then (64 ASCII SHA-256 bytes, LE uint32 size, WebP bytes).
// Sprite identity hashes its complete descriptor (including frame dimensions and
// team layers), so separate catalogs can share a mount without changing a path's
// contents. Validation needs no graphics context; mounting is an owner-thread task.
class BuildingArtwork
{
  public:
	static constexpr std::size_t MaxBytes = 72u * 1024u * 1024u;
	static std::shared_ptr<const BuildingArtwork> decode(std::string bytes,
														 const BuildingsTypes &catalog);
	const std::string &bytes() const { return encoded; }
	const GAGCore::AssetLoader::CommunityFiles &files() const { return mounted; }

  private:
	std::string encoded;
	GAGCore::AssetLoader::CommunityFiles mounted;
};
