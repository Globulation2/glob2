// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorPackage.h"
#include "GeneratorRegistry.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "Sha256.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace MapGeneration::JavaScript
{
using Json = nlohmann::json;
namespace
{
constexpr size_t PackageLimit = 4 * 1024 * 1024;
constexpr size_t LibraryLimit = 64 * 1024 * 1024;
constexpr size_t LibraryPackageLimit = 128;
constexpr const char *LibraryPath = "generators/library.json";
void require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::invalid_argument(message);
}
bool modulePath(const std::string &path)
{
	if (path.empty() || path.size() > 256 || path.front() == '/' ||
		path.find('\\') != std::string::npos || path.find('\0') != std::string::npos)
		return false;
	std::filesystem::path p(path);
	for (const auto &part : p)
		if (part == ".." || part == "." || part.empty())
			return false;
	return p.extension() == ".js";
}
std::string readFile(const std::filesystem::path &p)
{
	std::ifstream file(p, std::ios::binary);
	if (!file)
		throw std::runtime_error("Cannot read generator package: " + p.string());
	std::string bytes;
	char buffer[8192];
	while (file.read(buffer, sizeof(buffer)) || file.gcount())
	{
		bytes.append(buffer, size_t(file.gcount()));
		require(bytes.size() <= PackageLimit, "Generator package exceeds 4 MiB");
	}
	if (file.bad())
		throw std::runtime_error("Cannot read generator package: " + p.string());
	return bytes;
}
void parseMetadata(Package &p, const Json &m)
{
	p.id = m.at("id").get<std::string>();
	require(p.id.size() <= 128 && p.id.find(':') != std::string::npos && p.id.front() != ':' &&
				p.id.back() != ':' &&
				p.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_.:") ==
					std::string::npos,
			"Generator ID must be namespaced, for example author:landscape");
	p.name = m.at("name").get<std::string>();
	require(!p.name.empty() && p.name.size() <= 256 && p.name.find('\0') == std::string::npos,
			"Invalid generator name");
	p.description = m.value("description", "");
	p.author = m.value("author", "");
	require(p.description.size() <= 4096 && p.author.size() <= 256,
			"Generator metadata exceeds limit");
	require(m.at("revision").is_number_integer(), "Generator revision must be an integer");
	auto rev = m.at("revision").get<int64_t>();
	require(rev > 0 && rev <= UINT32_MAX, "Invalid generator revision");
	p.revision = unsigned(rev);
	p.entry = m.value("entry", "generator.js");
	p.editorOnly = m.value("editorOnly", false);
	p.hasStartingColonies = m.value("hasStartingColonies", true);
	require(p.hasStartingColonies || p.editorOnly,
			"Generators without starting colonies must be editor-only");
	p.tags = m.at("tags").get<std::vector<std::string>>();
	require(p.tags.size() <= 32, "Too many generator tags");
	for (const auto &tag : p.tags)
		require(tag.size() <= 128 && tag.find(':') != std::string::npos, "Invalid catalog tag");
}
void parseControls(Package &p, const Json &m)
{
	auto label = [&](const std::string &value)
	{
		require(!value.empty() && value.size() <= 256 && value.find('\0') == std::string::npos,
				"Invalid control label");
		p.labels.push_back(std::make_shared<const std::string>(value));
		return p.labels.back()->c_str();
	};
	const auto cs = m.value("controls", Json::array());
	require(cs.is_array() && cs.size() <= 64, "Invalid generator controls");
	for (const auto &v : cs)
	{
		auto id = v.at("id").get<std::string>();
		require(!id.empty() && id.size() <= 64 &&
					id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") ==
						std::string::npos,
				"Invalid control ID");
		const char *text = label(v.at("label"));
		std::string group = v.value("group", "layout");
		require(group == "layout" || group == "terrain" || group == "resources",
				"Invalid control group");
		auto g = group == "terrain"     ? ControlGroup::Terrain
				 : group == "resources" ? ControlGroup::Resources
										: ControlGroup::Layout;
		std::string kind = v.value("kind", "range");
		GeneratorControl c{id, text, 0, 1, 1, 0, g};
		if (kind == "toggle")
		{
			auto value = v.at("default").get<int>();
			require(value == 0 || value == 1, "Toggle default must be zero or one");
			c = GeneratorControl::toggle(id, text, value != 0, g);
		}
		else if (kind == "choice")
		{
			require(v.at("choices").is_array() && v.at("choices").size() <= 256,
					"Too many choices");
			std::vector<const char *> names;
			for (const auto &n : v.at("choices"))
				names.push_back(label(n));
			c = GeneratorControl::choice(id, text, names, v.at("default"), g);
		}
		else
		{
			require(kind == "range", "Invalid control kind");
			c.minimum = v.at("minimum");
			c.maximum = v.at("maximum");
			c.step = v.value("step", 1);
			c.defaultValue = v.at("default");
			c.allowedValues = v.value("values", std::vector<int>{});
			c.powerOfTwo = v.value("powerOfTwo", false);
			c.terrainWeight = v.value("terrainWeight", false);
		}
		require(c.step > 0 && c.minimum <= c.maximum && c.allowedValues.size() <= 4096 &&
					(!c.allowedValues.empty() ||
					 (std::int64_t(c.maximum) - c.minimum) / c.step + 1 <= 4096),
				"Control legal domain exceeds limit");
		require(v.contains("searchRange") != v.contains("searchValues"),
				"Declare exactly one control search domain");
		if (v.contains("searchRange"))
		{
			const auto &r = v.at("searchRange");
			require(r.is_array() && r.size() == 2, "Invalid search range");
			c = c.withSearchRange(r[0], r[1]);
		}
		else
		{
			require(v.at("searchValues").is_array() && v.at("searchValues").size() <= 4096,
					"Control search domain exceeds limit");
			c = c.withSearchValues(v.at("searchValues").get<std::vector<int>>());
		}
		p.controls.push_back(std::move(c));
	}
}
void parseModules(Package &p, const Json &modules)
{
	require(modules.is_object() && modules.size() <= 128, "Invalid generator modules");
	for (auto it = modules.begin(); it != modules.end(); ++it)
	{
		require(modulePath(it.key()) && it.value().is_string(),
				"Invalid generator module path or source");
		auto source = it.value().get<std::string>();
		require(source.find('\0') == std::string::npos, "Generator source contains NUL");
		p.modules.emplace(it.key(), std::move(source));
	}
	require(modulePath(p.entry) && p.modules.contains(p.entry),
			"Generator entry module is missing");
}
void parseTranslations(Package &p, const Json &m)
{
	p.translations = m.value("translations", decltype(p.translations){});
	require(p.translations.size() <= 64, "Too many translation languages");
	for (const auto &[language, entries] : p.translations)
	{
		require(!language.empty() && language.size() <= 32 && entries.size() <= 256,
				"Invalid translations");
		for (const auto &[key, value] : entries)
			require(key.size() <= 256 && value.size() <= 4096 &&
						key.find('\0') == std::string::npos &&
						value.find('\0') == std::string::npos,
					"Invalid translation text");
	}
}
} // namespace
std::shared_ptr<const Package> Package::parse(const std::string &bytes)
{
	require(bytes.size() <= PackageLimit, "Generator package exceeds 4 MiB");
	Json root = Json::parse(bytes);
	require(root.at("formatVersion").is_number_integer() && root.at("formatVersion") == 1,
			"Unsupported generator package format");
	const auto &m = root.at("manifest");
	require(m.at("apiVersion").is_number_integer() && m.at("apiVersion") == ApiVersion,
			"Unsupported generator API version");
	auto p = std::make_shared<Package>();
	parseMetadata(*p, m);
	parseControls(*p, m);
	parseModules(*p, root.at("modules"));
	parseTranslations(*p, m);
	p->canonical = root.dump();
	p->hash = Online::Sha256::hex(p->canonical);
	// First validate the data against the native catalog contracts. Only then
	// initialize executable modules, without a world or random-number access.
	auto probe = p->definition(1000000);
	GeneratorRegistry validation({std::move(probe)});
	inspect(*p);
	return p;
}
std::shared_ptr<const Package> Package::load(const std::string &path)
{
	if (!std::filesystem::is_directory(path))
		return parse(readFile(path));
	require(!std::filesystem::is_symlink(path), "Generator package root may not be a symlink");
	auto root = std::filesystem::canonical(path);
	Json manifest = Json::parse(readFile(root / "manifest.json"));
	Json package{{"formatVersion", 1}, {"manifest", manifest}, {"modules", Json::object()}};
	for (const auto &file : std::filesystem::recursive_directory_iterator(root))
	{
		require(!file.is_symlink(), "Generator package directories may not contain symlinks");
		if (file.is_regular_file() && file.path().extension() == ".js")
		{
			auto name = std::filesystem::relative(file.path(), root).generic_string();
			require(modulePath(name), "Invalid generator module path");
			package["modules"][name] = readFile(file.path());
			require(package["modules"].size() <= 128 && package.dump().size() <= PackageLimit,
					"Generator package exceeds limit");
		}
	}
	return parse(package.dump());
}
GeneratorDefinition Package::definition(int handle) const
{
	auto p = shared_from_this();
	GeneratorDefinition d{id.c_str(),
						  handle,
						  name.c_str(),
						  revision,
						  editorOnly,
						  controls,
						  [p](Game &game, GenerationContext &context)
						  {
							  auto error = invoke(*p, "generate", context.request, &game, &context);
							  if (!error.empty())
								  throw GenerationFailure(error);
							  return true;
						  },
						  hasStartingColonies};
	d.validateRequest = [p](const GenerationRequest &r)
	{
		GenerationContext c(r);
		return invoke(*p, "validateRequest", r, nullptr, &c, true);
	};
	d.validateWorld = [p](const Game &g, const GenerationContext &c)
	{
		GenerationContext probe(c.request, c.telemetry.enabled());
		return invoke(*p, "validateWorld", c.request, const_cast<Game *>(&g), &probe, true);
	};
	d.translate = [p](std::string_view language, std::string_view text)
	{
		auto it = p->translations.find(std::string(language));
		if (it != p->translations.end())
		{
			auto entry = it->second.find(std::string(text));
			if (entry != it->second.end())
				return entry->second;
		}
		return std::string(text);
	};
	d.tags = tags;
	d.owner = p;
	d.packageHash = hash;
	d.apiVersion = ApiVersion;
	return d;
}
Library::Library(Online::OnlineStorage &s) : storage(s)
{
	std::string bytes;
	if (storage.read(LibraryPath, bytes))
		packages = decode(bytes, provenance);
}
std::string Library::encode(const PackageMap &candidate, const Origins &origins)
{
	Json entries = Json::array();
	for (const auto &[id, p] : candidate)
		entries.push_back(Json::parse(p->canonical));
	Json sources = Json::object();
	for (const auto &[id, o] : origins)
		sources[id] = {{"origin", o.origin},
					   {"libraryId", o.libraryId},
					   {"versionId", o.versionId},
					   {"fileHash", o.fileHash},
					   {"packageHash", o.packageHash}};
	return Json{{"version", 1}, {"packages", entries}, {"origins", sources}}.dump();
}
Library::PackageMap Library::decode(const std::string &bytes, Origins &origins)
{
	require(bytes.size() <= LibraryLimit, "Generator library exceeds limit");
	auto root = Json::parse(bytes);
	require(root.at("version").is_number_integer() && root.at("version") == 1 &&
				root.at("packages").is_array() && root.at("packages").size() <= LibraryPackageLimit,
			"Invalid generator library");
	PackageMap loaded;
	for (const auto &v : root.at("packages"))
	{
		auto p = Package::parse(v.dump());
		require(loaded.emplace(p->id, p).second, "Duplicate generator ID");
	}
	origins.clear();
	if (root.contains("origins"))
		for (const auto &[id, v] : root["origins"].items())
		{
			require(loaded.contains(id), "Unknown generator provenance");
			origins[id] = {v.at("origin"), v.at("libraryId"), v.at("versionId"), v.at("fileHash"),
						   v.at("packageHash")};
			require(origins[id].packageHash == loaded.at(id)->hash,
					"Generator provenance hash mismatch");
		}
	return loaded;
}
void Library::save(PackageMap candidate, Origins origins)
{
	require(candidate.size() <= LibraryPackageLimit, "Generator library exceeds package limit");
	const auto bytes = encode(candidate, origins);
	require(bytes.size() <= LibraryLimit, "Generator library exceeds limit");
	// A false result or exception leaves both the active catalog and this library
	// unchanged. OnlineStorage replaces its file only after a complete write.
	if (!storage.write(LibraryPath, bytes))
		throw std::runtime_error("Could not save generator library");
	packages.swap(candidate);
	provenance.swap(origins);
}
void Library::rollback(const std::string &bytes)
{
	Origins origins;
	auto candidate = decode(bytes, origins);
	save(std::move(candidate), std::move(origins));
}
void Library::put(const std::string &bytes, const std::string &replace, const LibraryOrigin *origin)
{
	auto p = Package::parse(bytes);
	require(replace.empty() ? !packages.contains(p->id)
							: replace == p->id && packages.contains(replace),
			"Duplicate generator ID or replacement identity mismatch");
	auto candidate = packages;
	candidate[p->id] = p;
	auto origins = provenance;
	origins.erase(p->id);
	if (origin)
	{
		require(Online::Sha256::hex(bytes) == origin->fileHash && p->hash == origin->packageHash,
				"Generator download hash mismatch");
		origins[p->id] = *origin;
	}
	save(std::move(candidate), std::move(origins));
}
void Library::remove(const std::string &id)
{
	auto candidate = packages;
	require(candidate.erase(id) != 0, "Unknown custom generator");
	auto origins = provenance;
	origins.erase(id);
	save(std::move(candidate), std::move(origins));
}
void Library::publish() const
{
	static std::mutex mutex;
	static std::map<std::string, int> handles;
	static int next = 1000000;
	std::lock_guard lock(mutex);
	auto entries = GeneratorRegistry::builtins().entries();
	for (const auto &[id, p] : packages)
	{
		if (!handles.contains(id))
			handles[id] = next++;
		entries.push_back(p->definition(handles.at(id)));
	}
	GeneratorRegistry::publish(std::make_shared<const GeneratorRegistry>(std::move(entries)));
}
} // namespace MapGeneration::JavaScript
