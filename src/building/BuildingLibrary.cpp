// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingLibrary.h"
#include "OnlineStorage.h"
#include "Sha256.h"
#include "SimVersion.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <map>
using Json = nlohmann::json;
namespace
{
Json parse(const std::string &text)
{
	return Json::parse(text,
					   [](int depth, Json::parse_event_t, Json &)
					   {
						   if (depth > 64)
							   throw std::runtime_error(
								   "Building library JSON nesting exceeds its limit");
						   return true;
					   });
}
const std::string root = "online/buildings/", libraryIndex = root + "installed.json";
std::string read(Online::OnlineStorage &storage, const std::string &path, std::size_t max)
{
	if (storage.size(path) > max)
		throw std::runtime_error("Installed building file exceeds its limit");
	std::string result;
	if (!storage.read(path, result) || result.size() > max)
		throw std::runtime_error("Cannot read installed building release");
	return result;
}
std::string artworkBundle(const std::vector<std::shared_ptr<const BuildingArtwork>> &bundles)
{
	// Inputs have already passed BuildingArtwork::decode. Merge descriptors by
	// their canonical content identity; this keeps package order irrelevant and
	// avoids duplicating shared image bytes in the portable map payload.
	std::map<std::string, Json> sprites;
	std::map<std::string, std::string> assets;
	for (const auto &bundle : bundles)
	{
		const auto &bytes = bundle->bytes();
		std::size_t at = 8;
		const auto integer = [&]()
		{
			std::uint32_t n = 0;
			for (unsigned i = 0; i < 4; ++i)
				n |= std::uint32_t(static_cast<unsigned char>(bytes.at(at++))) << (i * 8);
			return n;
		};
		const auto size = integer();
		const auto descriptors = parse(bytes.substr(at, size));
		at += size;
		for (const auto &sprite : descriptors)
			sprites.emplace(Online::Sha256::hex(sprite.dump()), sprite);
		const auto count = integer();
		for (unsigned i = 0; i < count; ++i)
		{
			const auto hash = bytes.substr(at, 64);
			at += 64;
			const auto length = integer();
			assets.emplace(hash, bytes.substr(at, length));
			at += length;
		}
	}
	if (sprites.empty())
		return {};
	Json descriptors = Json::array();
	for (const auto &[hash, sprite] : sprites)
		descriptors.push_back(sprite);
	std::string bytes = "G2BA0001";
	const auto integer = [&](std::uint32_t n)
	{
		for (unsigned i = 0; i < 4; ++i)
			bytes += char((n >> (8 * i)) & 255);
	};
	const auto text = descriptors.dump();
	integer(text.size());
	bytes += text;
	integer(assets.size());
	for (const auto &[hash, image] : assets)
	{
		bytes += hash;
		integer(image.size());
		bytes += image;
	}
	if (bytes.size() > BuildingArtwork::MaxBytes)
		throw std::runtime_error("Selected artwork exceeds its limit");
	return bytes;
}
} // namespace
Json BuildingLibrary::entries() const
{
	std::string text;
	if (storage.size(libraryIndex) > 128 * 1024)
		throw std::runtime_error("Installed building index exceeds its limit");
	if (!storage.read(libraryIndex, text))
		return Json::array();
	if (text.size() > 128 * 1024)
		throw std::runtime_error("Installed building index exceeds its limit");
	auto result = parse(text);
	if (!result.is_array() || result.size() > 100)
		throw std::runtime_error("Invalid installed building index");
	return result;
}
void BuildingLibrary::save(const Json &value)
{
	if (!storage.write(libraryIndex, value.dump()))
		throw std::runtime_error("Cannot save installed building selection");
	storage.persist();
}
void BuildingLibrary::install(const Json &manifest, const std::string &artwork)
{
	if (!manifest.is_object() || manifest.value("schemaVersion", 0) != 1 ||
		manifest.at("simVersion") != Online::SimVersion::local().key())
		throw std::runtime_error("This building release needs a different game version");
	const auto hash = manifest.at("archiveHash").get<std::string>();
	if (!Online::Sha256::isHexDigest(hash))
		throw std::runtime_error("Invalid release hash");
	// Preserve the server's exact canonical bytes. Re-serializing numbers through
	// another JSON implementation can change package and catalog identities.
	const auto pkg = manifest.at("packageJson").get<std::string>();
	if (pkg.size() > 8 * 1024 * 1024 ||
		Online::Sha256::hex(pkg) != manifest.at("packageHash").get<std::string>())
		throw std::runtime_error("Building package integrity check failed");
	auto catalog = globalContainer->buildingsTypes;
	if (catalog.fingerprint() != manifest.at("baseHash").get<std::string>())
		throw std::runtime_error("This release needs a different stock building catalog");
	catalog.composePackages({pkg});
	if (catalog.fingerprint() != manifest.at("catalogHash").get<std::string>())
		throw std::runtime_error("Building catalog integrity check failed");
	const auto decoded = BuildingArtwork::decode(artwork, catalog);
	if ((decoded ? Online::Sha256::hex(artwork) : std::string{}) !=
		manifest.value("artworkHash", std::string{}))
		throw std::runtime_error("Building artwork integrity check failed");
	if (!manifest.at("name").is_string() || manifest.at("name").get<std::string>().size() > 512)
		throw std::runtime_error("Invalid family name");
	auto installed = entries();
	const auto ns = manifest.at("namespace").get<std::string>();
	if (parse(pkg).at("namespace") != ns)
		throw std::runtime_error("Building namespace mismatch");
	auto old = std::find_if(installed.begin(), installed.end(),
							[&](const auto &e) { return e.at("namespace") == ns; });
	if (old == installed.end() && installed.size() >= 100)
		throw std::runtime_error("The installed building library has 100 families");
	std::size_t used = manifest.dump().size() + artwork.size();
	for (const auto &e : installed)
		if (e.at("namespace") != ns)
		{
			const auto h = e.at("archiveHash").get<std::string>();
			if (!Online::Sha256::isHexDigest(h))
				throw std::runtime_error("Invalid installed release hash");
			used += storage.size(root + h + ".json") + storage.size(root + h + ".g2ba");
		}
	if (used > 64 * 1024 * 1024)
		throw std::runtime_error(
			"Installed building releases exceed 64 MiB; remove an old family first");
	const std::string previousHash =
		old == installed.end() ? std::string{} : old->at("archiveHash").get<std::string>();
	// The index changes last: interrupted writes never select a partial release.
	if (!storage.write(root + hash + ".json", manifest.dump()) ||
		(!artwork.empty() && !storage.write(root + hash + ".g2ba", artwork)))
		throw std::runtime_error("Cannot store building release");
	Json entry = {{"namespace", ns},
				  {"name", manifest.at("name")},
				  {"archiveHash", hash},
				  {"simVersion", manifest.at("simVersion")},
				  {"baseHash", manifest.at("baseHash")},
				  {"selected", old != installed.end() && old->value("selected", false)}};
	if (old == installed.end())
		installed.push_back(entry);
	else
		*old = entry;
	save(installed);
	if (!previousHash.empty() && previousHash != hash)
	{
		storage.remove(root + previousHash + ".json");
		storage.remove(root + previousHash + ".g2ba");
		storage.persist();
	}
}
void BuildingLibrary::select(const std::string &ns, bool enabled)
{
	auto installed = entries();
	bool found = false;
	for (auto &e : installed)
		if (e.at("namespace") == ns)
		{
			e["selected"] = enabled;
			found = true;
		}
	if (!found)
		throw std::runtime_error("No installed family");
	save(installed);
}
void BuildingLibrary::remove(const std::string &ns)
{
	auto installed = entries();
	for (auto it = installed.begin(); it != installed.end(); ++it)
		if (it->at("namespace") == ns)
		{
			const auto hash = it->at("archiveHash").get<std::string>();
			if (!Online::Sha256::isHexDigest(hash))
				throw std::runtime_error("Invalid installed release hash");
			installed.erase(it);
			save(installed);
			storage.remove(root + hash + ".json");
			storage.remove(root + hash + ".g2ba");
			storage.persist();
			return;
		}
}
BuildingLibrary::Selection BuildingLibrary::compose(const BuildingsTypes &stock) const
{
	Selection result{stock, {}};
	std::vector<std::string> packages;
	std::vector<std::shared_ptr<const BuildingArtwork>> artwork;
	std::size_t size = 0, imageBytes = 0, decodedBytes = 0;
	for (const auto &entry : entries())
		if (entry.value("selected", false))
		{
			const auto hash = entry.at("archiveHash").get<std::string>();
			if (!Online::Sha256::isHexDigest(hash))
				throw std::runtime_error("Invalid installed release hash");
			const auto manifest = parse(read(storage, root + hash + ".json", 18 * 1024 * 1024));
			if (manifest.at("archiveHash") != hash ||
				manifest.at("namespace") != entry.at("namespace") ||
				manifest.at("simVersion") != Online::SimVersion::local().key() ||
				manifest.at("baseHash").get<std::string>() != stock.fingerprint())
				throw std::runtime_error(
					"Installed release is incompatible; download it for this game version");
			const auto package = parse(manifest.at("packageJson").get<std::string>());
			for (const auto &sprite : package.at("sprites"))
				for (const auto &frame : sprite.at("frames"))
				{
					decodedBytes += frame.at("width").get<std::size_t>() *
									frame.at("height").get<std::size_t>() * 4 *
									(frame.contains("teamColorHash") ? 2 : 1);
					if (decodedBytes > 64 * 1024 * 1024)
						throw std::runtime_error("Selected artwork exceeds 64 MiB decoded");
				}
			const auto pkg = manifest.at("packageJson").get<std::string>();
			size += pkg.size();
			if (size > 8 * 1024 * 1024 ||
				Online::Sha256::hex(pkg) != manifest.at("packageHash").get<std::string>())
				throw std::runtime_error("Installed package integrity check failed");
			auto catalog = stock;
			catalog.composePackages({pkg});
			if (catalog.fingerprint() != manifest.at("catalogHash").get<std::string>())
				throw std::runtime_error("Installed catalog integrity check failed");
			std::string bytes;
			if (manifest.contains("artworkHash"))
				bytes = read(storage, root + hash + ".g2ba", BuildingArtwork::MaxBytes);
			imageBytes += bytes.size();
			if (imageBytes > 64 * 1024 * 1024)
				throw std::runtime_error("Selected artwork exceeds 64 MiB encoded");
			if ((bytes.empty() ? std::string{} : Online::Sha256::hex(bytes)) !=
				manifest.value("artworkHash", std::string{}))
				throw std::runtime_error("Installed artwork integrity check failed");
			if (auto decoded = BuildingArtwork::decode(std::move(bytes), catalog))
				artwork.push_back(std::move(decoded));
			packages.push_back(pkg);
		}
	if (!packages.empty())
		result.catalog.composePackages(packages);
	result.artwork = BuildingArtwork::decode(artworkBundle(artwork), result.catalog);
	return result;
}
