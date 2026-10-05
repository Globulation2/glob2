// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainRegistry.h"
#include "online/Sha256.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <stdexcept>
#include <type_traits>

namespace
{
using Json = nlohmann::json;
void fields(const Json &j, std::initializer_list<const char *> allowed)
{
	if (!j.is_object())
		throw std::invalid_argument("Terrain definition must be an object");
	for (const auto &item : j.items())
		if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
			throw std::invalid_argument("Unknown terrain field: " + item.key());
}
std::string text(const Json &j, const char *key,
				 std::size_t limit = TerrainRegistry::MaximumTextBytes)
{
	const auto &v = j.at(key);
	if (!v.is_string())
		throw std::invalid_argument(std::string(key) + " must be text");
	auto s = v.get<std::string>();
	if (s.empty() || s.size() > limit || s.find('\0') != std::string::npos)
		throw std::invalid_argument(std::string("Invalid terrain ") + key);
	return s;
}
TerrainType preset(const std::string &name)
{
	for (unsigned i = 0; i < GRASS_SAND_SHORE; ++i)
		if (name == TerrainPresentations[i].name)
			return TerrainType(i);
	throw std::invalid_argument("Unknown built-in terrain preset: " + name);
}
void validKey(const std::string &key)
{
	const auto colon = key.find(':');
	if (colon == std::string::npos || !colon || colon + 1 == key.size() ||
		key.starts_with("glob2:"))
		throw std::invalid_argument("Terrain keys require a non-reserved namespace:name");
	for (unsigned char c : key)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '_' ||
			  c == '-' || c == '.'))
			throw std::invalid_argument(
				"Terrain keys use lowercase ASCII letters, digits, colon, dot, dash or underscore");
	if (key.find(':', colon + 1) != std::string::npos)
		throw std::invalid_argument("Invalid terrain namespace");
}
template <class T> void property(const Json &j, const char *key, T &value, bool required = false)
{
	auto it = j.find(key);
	if (it == j.end())
	{
		if (required)
			throw std::invalid_argument(std::string("Missing terrain field: ") + key);
		return;
	}
	if constexpr (std::is_same_v<T, bool>)
	{
		if (!it->is_boolean())
			throw std::invalid_argument(std::string(key) + " must be boolean");
		value = it->get<bool>();
	}
	else
	{
		if (!it->is_number_integer() ||
			(it->is_number_unsigned() &&
			 it->get<std::uint64_t>() > std::uint64_t(std::numeric_limits<T>::max())))
			throw std::invalid_argument(std::string(key) + " must be an in-range integer");
		const auto n = it->get<std::int64_t>();
		if (n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
			throw std::invalid_argument(std::string(key) + " is out of range");
		value = static_cast<T>(n);
	}
}
// One field list drives validation, persistence and property deduplication.
// Keep serialization field-wise: struct padding is neither portable nor stable.
// clang-format off
#define TERRAIN_FIELDS(X) \
	X(walkable) \
	X(swimmable) \
	X(flyable) \
	X(resourcesGrow) \
	X(fertilitySource) \
	X(nonGrowingResources) \
	X(buildable) \
	X(projectileBlocks) \
	X(shoreline) \
	X(groundSpeedQ8) \
	X(airSpeedQ8) \
	X(groundHealthQ8) \
	X(airHealthQ8) \
	X(growthQ8) \
	X(fertilityQ8) \
	X(inhibitionQ8) \
	X(shoreSupportQ8) \
	X(allowedResources) \
	X(farmCrop)
// clang-format on
TerrainProperties readProperties(const Json &j, TerrainProperties p, bool complete = false)
{
#define NAME(f) #f,
	fields(j, {TERRAIN_FIELDS(NAME)});
#undef NAME
#define READ(f) property(j, #f, p.f, complete);
	TERRAIN_FIELDS(READ)
#undef READ
	if (!validTerrainProperties(p))
		throw std::invalid_argument("Invalid terrain property combination or range");
	return p;
}
Json writeProperties(const TerrainProperties &p)
{
	Json j = Json::object();
#define WRITE(f) j[#f] = p.f;
	TERRAIN_FIELDS(WRITE)
#undef WRITE
	return j;
}
auto propertyKey(const TerrainProperties &p)
{
#define VALUE(f) int(p.f),
	return std::to_array<int>({TERRAIN_FIELDS(VALUE)});
#undef VALUE
}
#undef TERRAIN_FIELDS

Json parse(std::string_view source)
{
	if (source.size() > TerrainRegistry::MaximumDefinitionBytes)
		throw std::invalid_argument("Terrain definitions exceed 32 MiB");
	// Reject duplicate JSON object members instead of accepting last-wins input.
	std::vector<std::set<std::string>> keys;
	auto callback = [&](int depth, Json::parse_event_t event, Json &value)
	{
		// Both schema variants are shallow. Bound malformed nesting before a
		// large attacker-controlled JSON tree is constructed.
		if (depth > 16)
			throw std::invalid_argument("Terrain definitions are nested too deeply");
		if (event == Json::parse_event_t::object_start)
			keys.emplace_back();
		if (event == Json::parse_event_t::key &&
			!keys.back().insert(value.get<std::string>()).second)
			throw std::invalid_argument("Duplicate terrain JSON field");
		if (event == Json::parse_event_t::object_end)
			keys.pop_back();
		return true;
	};
	auto j = Json::parse(source, callback);
	fields(j, {"schemaVersion", "terrains"});
	if (j.at("schemaVersion") != 1 || !j.at("schemaVersion").is_number_integer() ||
		!j.at("terrains").is_array() ||
		j.at("terrains").size() > TerrainRegistry::Capacity - TERRAIN_COUNT)
		throw std::invalid_argument("Unsupported terrain schema or definition count");
	return j;
}
} // namespace

TerrainRegistry::SavedPresentation TerrainRegistry::savedPreset(TerrainType appearance)
{
	SavedPresentation result;
	static_cast<TerrainCompatibility &>(result) = terrainCompatibility(appearance);
	// Frozen format-136 defaults, unrelated to the current terrain material pack.
	result.editorFrame = appearance == WATER ? 259 : result.firstFrame;
	result.animatedBackdrop = appearance == WATER;
	if (appearance == ICE)
	{
		result.edgeFirstFrame = 304;
		result.layerPriority = 2;
	}
	else if (appearance == TRAIL)
	{
		result.edgeFirstFrame = 319;
		result.layerPriority = 1;
	}
	return result;
}

TerrainRegistry::TerrainRegistry()
	: properties_(TERRAIN_PROPERTIES.begin(), TERRAIN_PROPERTIES.end()),
	  presentations_(TerrainPresentations.begin(), TerrainPresentations.end())
{
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
	{
		keys_.emplace_back(TerrainPresentations[i].name);
		names_.emplace_back(TerrainPresentations[i].label);
		appearances_.push_back(TerrainType(i));
		savedPresentations_.push_back(savedPreset(TerrainType(i)));
	}
}
std::shared_ptr<const TerrainRegistry> TerrainRegistry::builtins()
{
	static const auto result = []
	{
		auto p = std::shared_ptr<TerrainRegistry>(new TerrainRegistry);
		p->compile();
		return p;
	}();
	return result;
}
std::shared_ptr<const TerrainRegistry> TerrainRegistry::importJson(std::string_view source) const
{
	auto j = parse(source);
	auto result = std::shared_ptr<TerrainRegistry>(new TerrainRegistry(*this));
	std::set<std::string> imported;
	auto &definitions = j.at("terrains");
	for (const auto &item : definitions)
	{
		fields(item, {"key", "name", "base", "properties", "appearance"});
		auto key = text(item, "key");
		validKey(key);
		if (!imported.insert(key).second)
			throw std::invalid_argument("Duplicate terrain key: " + key);
	}
	std::sort(definitions.begin(), definitions.end(),
			  [](const auto &a, const auto &b)
			  {
				  return a.at("key").template get<std::string>() <
						 b.at("key").template get<std::string>();
			  });
	std::unordered_map<std::string, std::size_t> ids;
	ids.reserve(result->size() + definitions.size());
	for (std::size_t i = 0; i < result->size(); ++i)
		ids.emplace(result->keys_[i], i);
	for (const auto &item : definitions)
	{
		const auto key = text(item, "key"), name = text(item, "name");
		const auto base = preset(text(item, "base")), appearance = preset(text(item, "appearance"));
		auto p = readProperties(item.at("properties"), TERRAIN_PROPERTIES[base]);
		const auto id = ids.try_emplace(key, result->size()).first->second;
		if (id == result->size())
		{
			if (id >= Capacity)
				throw std::invalid_argument("Too many terrain types");
			result->keys_.push_back(key);
			result->names_.push_back(name);
			result->properties_.push_back(p);
			result->appearances_.push_back(appearance);
			result->presentations_.push_back(TerrainPresentations[appearance]);
			result->savedPresentations_.push_back(savedPreset(appearance));
		}
		else
		{
			result->names_[id] = name;
			result->properties_[id] = p;
			result->appearances_[id] = appearance;
			result->presentations_[id] = TerrainPresentations[appearance];
			result->savedPresentations_[id] = savedPreset(appearance);
		}
		result->savedPresentations_[id].legacyCorners = false;
		result->presentations_[id].editorSelectable = true;
	}
	// Release the authoring DOM before compiling/canonical hashing large registries.
	j.clear();
	result->compile();
	return result;
}
std::string TerrainRegistry::serialize() const
{
	// Stream one definition at a time. Building a second full JSON DOM during
	// import/load would make peak memory proportional to two expanded registries.
	// The envelope and sorted per-definition keys preserve canonical JSON bytes.
	std::string result = R"({"schemaVersion":1,"terrains":[)";
	for (unsigned i = TERRAIN_COUNT; i < size(); ++i)
	{
		const auto &p = savedPresentations_[i];
		const auto &colors = presentations_[i];
		Json visual = {{"firstFrame", p.firstFrame},
					   {"variants", p.variants},
					   {"editorFrame", p.editorFrame},
					   {"animatedBackdrop", p.animatedBackdrop},
					   {"edgeFirstFrame", p.edgeFirstFrame},
					   {"layerPriority", p.layerPriority},
					   {"animationFrames", p.animationFrames},
					   {"animationTicks", p.animationTicks},
					   {"backdropFirstFrame", p.backdropFirstFrame},
					   {"backdropFrames", p.backdropFrames},
					   {"backdropTicks", p.backdropTicks}};
		auto color = [](TerrainColor c) { return Json::array({c.r, c.g, c.b}); };
		visual["minimap"] = color(colors.minimap);
		visual["overview"] = color(colors.overview);
		visual["image"] = color(colors.image);
		visual["preview"] = color(colors.preview);
		if (i != TERRAIN_COUNT)
			result += ',';
		result += Json{
			{"id", i},
			{"key", keys_[i]},
			{"name", names_[i]},
			{"properties", writeProperties(properties_[i])},
			{"appearance", TerrainPresentations[appearances_[i]].name},
			{"presentation",
			 visual}}.dump();
	}
	result += "]}";
	return result;
}
std::shared_ptr<const TerrainRegistry> TerrainRegistry::deserialize(std::string_view source)
{
	auto j = parse(source);
	if (j.at("terrains").empty())
		return builtins();
	auto result = std::shared_ptr<TerrainRegistry>(new TerrainRegistry);
	std::set<std::string> keys;
	for (const auto &item : j.at("terrains"))
	{
		fields(item, {"id", "key", "name", "properties", "appearance", "presentation"});
		if (!item.at("id").is_number_integer() || item.at("id") != result->size())
			throw std::invalid_argument("Invalid saved terrain ID");
		const auto key = text(item, "key");
		validKey(key);
		if (!keys.insert(key).second)
			throw std::invalid_argument("Duplicate saved terrain key");
		const auto appearance = preset(text(item, "appearance"));
		result->keys_.push_back(key);
		result->names_.push_back(text(item, "name"));
		result->properties_.push_back(readProperties(item.at("properties"), {}, true));
		result->appearances_.push_back(appearance);
		auto p = savedPreset(appearance);
		p.legacyCorners = false;
		auto presentation = TerrainPresentations[appearance];
		presentation.editorSelectable = true;
		const auto &visual = item.at("presentation");
		fields(visual,
			   {"firstFrame", "variants", "editorFrame", "animatedBackdrop", "edgeFirstFrame",
				"layerPriority", "animationFrames", "animationTicks", "backdropFirstFrame",
				"backdropFrames", "backdropTicks", "minimap", "overview", "image", "preview"});
		property(visual, "firstFrame", p.firstFrame, true);
		property(visual, "variants", p.variants, true);
		property(visual, "editorFrame", p.editorFrame, true);
		property(visual, "animatedBackdrop", p.animatedBackdrop, true);
		property(visual, "edgeFirstFrame", p.edgeFirstFrame, true);
		property(visual, "layerPriority", p.layerPriority, true);
		property(visual, "animationFrames", p.animationFrames, true);
		property(visual, "animationTicks", p.animationTicks, true);
		property(visual, "backdropFirstFrame", p.backdropFirstFrame, true);
		property(visual, "backdropFrames", p.backdropFrames, true);
		property(visual, "backdropTicks", p.backdropTicks, true);
		auto color = [&](const char *key)
		{
			const auto &c = visual.at(key);
			if (!c.is_array() || c.size() != 3)
				throw std::invalid_argument("Invalid saved terrain color");
			TerrainColor out{};
			unsigned n = 0;
			for (auto *channel : {&out.r, &out.g, &out.b})
			{
				Json v = {{"channel", c[n++]}};
				property(v, "channel", *channel);
			}
			return out;
		};
		presentation.minimap = color("minimap");
		presentation.overview = color("overview");
		presentation.image = color("image");
		presentation.preview = color("preview");
		// V1 only uses shipped frame ranges; a saved registry cannot expand the asset capability.
		const auto original = savedPreset(appearance);
		if (p.firstFrame != original.firstFrame || p.variants != original.variants ||
			p.editorFrame != original.editorFrame || p.edgeFirstFrame != original.edgeFirstFrame ||
			p.animationFrames != original.animationFrames || p.animationTicks < 1 ||
			p.backdropFirstFrame != original.backdropFirstFrame ||
			p.backdropFrames != original.backdropFrames || p.backdropTicks < 1 ||
			p.animatedBackdrop != original.animatedBackdrop)
			throw std::invalid_argument("Saved terrain references unsupported shipped artwork");
		result->presentations_.push_back(presentation);
		result->savedPresentations_.push_back(p);
	}
	j.clear();
	result->compile();
	return result;
}

void TerrainRegistry::Movement::prepare()
{
	if (profiles.empty() || profiles.size() > 128)
		throw std::logic_error("Invalid terrain movement profiles");
	auto make = [&]<std::size_t N>()
	{
		std::array<gradient_kernel::EntrySteps, N> costs;
		costs.fill(profiles.front());
		std::copy(profiles.begin(), profiles.end(), costs.begin());
		prepared = gradient_kernel::PreparedTerrainCosts<N>(costs);
	};
	if (profiles.size() <= 8)
		make.template operator()<8>();
	else
		make.template operator()<128>();
}

void TerrainRegistry::compile()
{
	// Field-wise keys exclude struct padding and preserve deterministic IDs.
	std::map<decltype(propertyKey(TerrainProperties{})), std::uint16_t> properties;
	propertyProfiles_.clear();
	propertyIndices_.clear();
	propertyProfiles_.reserve(size());
	propertyIndices_.reserve(size());
	for (const auto &p : properties_)
	{
		auto [it, inserted] = properties.emplace(propertyKey(p), propertyProfiles_.size());
		if (inserted)
			propertyProfiles_.push_back(p);
		propertyIndices_.push_back(it->second);
	}

	airCosts_.resize(size());
	minimumAirCost_ = GRADIENT_STEP;
	for (unsigned i = 0; i < size(); ++i)
	{
		if (i >= TERRAIN_COUNT)
		{
			presentations_[i].name = keys_[i].c_str();
			presentations_[i].label = names_[i].c_str();
		}
		const auto &p = properties_[i];
		airCosts_[i] = gradient_kernel::scaledTerrainStep(GRADIENT_STEP, p.airSpeedQ8);
		if (p.flyable)
			minimumAirCost_ = std::min(minimumAirCost_, airCosts_[i]);
	}
	for (unsigned sw = 0; sw < movement_.size(); ++sw)
	{
		auto &m = movement_[sw];
		m = {};
		m.entries.reserve(size());
		m.profileIds.reserve(size());
		std::array<bool, 256> used{};
		for (const auto &p : properties_)
		{
			auto cost = gradient_kernel::entrySteps(gradient_kernel::scaledTerrainStep(
				p.swimmable && sw ? gradient_kernel::WATER_STEP[sw] : GRADIENT_STEP,
				p.groundSpeedQ8));
			if (cost.cardinal == 0 || cost.diagonal >= 256)
				throw std::invalid_argument("Terrain edge exceeds supported gradient queue");
			m.entries.push_back(cost);
			unsigned profile = 0;
			while (profile < m.profiles.size() && (m.profiles[profile].cardinal != cost.cardinal ||
												   m.profiles[profile].diagonal != cost.diagonal))
				++profile;
			if (profile == m.profiles.size())
				m.profiles.push_back(cost);
			m.profileIds.push_back(static_cast<std::uint8_t>(profile));
			for (auto step : {cost.cardinal, cost.diagonal})
				if (!used[step])
				{
					used[step] = true;
					m.steps.push_back(step);
				}
			if (p.walkable || (sw && p.swimmable))
				m.minimum = std::min(m.minimum, cost.cardinal);
		}
		m.prepare();
	}
	checksum_ = 0;
	digest_.clear();
	if (size() > TERRAIN_COUNT)
	{
		auto hash = Online::Sha256::of(serialize());
		digest_ = Online::toHex(hash);
		for (unsigned i = 0; i < hash.size(); ++i)
			checksum_ ^= std::uint32_t(hash[i]) << ((i % 4) * 8);
	}
}

std::optional<TerrainType> TerrainRegistry::find(std::string_view key) const
{
	for (unsigned i = 0; i < size(); ++i)
		if (keys_[i] == key)
			return TerrainType(i);
	return std::nullopt;
}
