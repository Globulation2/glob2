// SPDX-License-Identifier: GPL-3.0-or-later
#include "CellRules.h"
#include <algorithm>
#include <stdexcept>

CellRuleTable::CellRuleTable(std::shared_ptr<const TerrainRegistry> terrains,
							 std::shared_ptr<const ResourceRegistry> resources)
	: terrains_(std::move(terrains)), resources_(std::move(resources))
{
	// Compact distinct resource habitat profiles, as resource definitions share them.
	resourceHabitatProfiles_.resize(resources_->size());
	for (unsigned id = 0; id < resources_->size(); ++id)
	{
		const auto &p = resources_->properties(static_cast<ResourceId>(id));
		const HabitatProfile profile{p.habitatMask, p.requiresGrowthTerrain, p.requiresPermanentDepositsTerrain};
		auto it = std::find(habitatProfiles_.begin(), habitatProfiles_.end(), profile);
		resourceHabitatProfiles_[id] = std::uint16_t(it - habitatProfiles_.begin());
		if (it == habitatProfiles_.end()) habitatProfiles_.push_back(profile);
	}
	habitatMaterials_.assign(habitatProfiles_.size(), 0);
	habitatCrops_.resize(habitatProfiles_.size());
	for (auto &crops : habitatCrops_) crops.fill(NO_RES_TYPE);
	for (unsigned id = 0; id < resources_->size(); ++id)
	{
		const auto &p = resources_->properties(static_cast<ResourceId>(id));
		const auto profile = resourceHabitatProfiles_[id];
		habitatMaterials_[profile] |= p.materialMask;
		if (p.farmable)
			for (unsigned m = 0; m < MaterialCount; ++m)
				if ((p.materialMask & (1u << m)) && habitatCrops_[profile][m] == NO_RES_TYPE)
					habitatCrops_[profile][m] = int(id);
	}
	typeAllowedResources_.assign(terrains_->size(), nullptr);
	for (unsigned t = 0; t < terrains_->size(); ++t)
	{
		const auto &keys = terrains_->resourceKeys(static_cast<TerrainType>(t));
		if (!keys) continue;
		std::vector<std::uint64_t> bits((resources_->size() + 63) / 64, 0);
		for (const auto &resourceKey : *keys)
		{
			const auto id = resources_->find(resourceKey);
			if (!id) throw std::invalid_argument("Unknown resource in terrain whitelist: " + resourceKey);
			const auto n = resourceIndex(*id);
			bits[n / 64] |= std::uint64_t(1) << (n % 64);
		}
		auto shared = std::make_shared<const std::vector<std::uint64_t>>(bits);
		typeAllowedResources_[t] = uniqueLists_.emplace(std::move(bits), shared).first->second;
	}
	for (unsigned t = 0; t < terrains_->size(); ++t)
	{
		const auto type = static_cast<TerrainType>(t);
		add(key(type, type, type, type), false);
	}
	for (auto &m : movement_) m.prepare();
}

CellRuleTable::Key CellRuleTable::key(TerrainType a, TerrainType b, TerrainType c, TerrainType d)
{
	Key result{a, b, c, d};
	std::sort(result.begin(), result.end());
	return result;
}

std::optional<std::uint16_t> CellRuleTable::find(const Key &key) const
{
	const auto it = index_.find(key);
	if (it == index_.end()) return std::nullopt;
	return it->second;
}

std::uint16_t CellRuleTable::intern(const Key &key)
{
	if (const auto found = find(key)) return *found;
	return add(key, true);
}

std::uint16_t CellRuleTable::add(const Key &key, bool prepare)
{
	for (const auto type : key)
		if (!terrains_->valid(type)) throw std::invalid_argument("Unknown terrain identity");
	if (rules_.size() >= Capacity) throw std::length_error("Too many distinct terrain corner combinations");
	CellRule rule;
	rule.corners = key;
	compile(rule);
	const auto id = std::uint16_t(rules_.size());
	for (unsigned sw = 0; sw < movement_.size(); ++sw)
	{
		auto &m = movement_[sw];
		const auto cost = rule.ground[sw];
		m.entries.push_back(cost);
		unsigned profile = 0;
		while (profile < m.profiles.size() && (m.profiles[profile].cardinal != cost.cardinal ||
											   m.profiles[profile].diagonal != cost.diagonal))
			++profile;
		if (profile == m.profiles.size())
		{
			if (profile == 256) throw std::length_error("Too many terrain movement profiles");
			m.profiles.push_back(cost);
			for (auto step : {cost.cardinal, cost.diagonal})
				if (std::find(m.steps.begin(), m.steps.end(), step) == m.steps.end()) m.steps.push_back(step);
		}
		m.profileIds.push_back(static_cast<std::uint8_t>(profile));
		const auto &p = rule.properties;
		if (p.walkable || (sw && p.swimmable)) m.minimum = std::min(m.minimum, cost.cardinal);
		if (prepare) m.prepare();
	}
	rules_.push_back(std::move(rule));
	index_.emplace(key, id);
	return id;
}

void CellRuleTable::compile(CellRule &rule)
{
	const auto &c = rule.corners;
	const auto &p = rule.properties = combineCornerRules(terrains_->properties(c[0]), terrains_->properties(c[1]),
														 terrains_->properties(c[2]), terrains_->properties(c[3]));
	for (unsigned sw = 0; sw < rule.ground.size(); ++sw)
	{
		const auto cost = gradient_kernel::terrainEntrySteps(p, int(sw));
		if (cost.cardinal == 0 || cost.diagonal >= 256)
			throw std::invalid_argument("Terrain edge exceeds supported gradient queue");
		rule.ground[sw] = cost;
	}
	rule.groundTravelCost = gradient_kernel::scaledTerrainStep(GRADIENT_STEP, p.groundSpeedQ8);
	rule.airCost = gradient_kernel::scaledTerrainStep(GRADIENT_STEP, p.airSpeedQ8);
	rule.airRouteCost = gradient_kernel::hazardRouteCost(rule.airCost, p.airHealthQ8);

	unsigned habitats = 0;
	if (p.walkable && !p.shoreline) habitats |= ResourceLand;
	if (p.swimmable) habitats |= ResourceAquatic;
	if (p.shoreline) habitats |= ResourceShore | ResourceDesert;
	rule.habitatRow = std::uint32_t(permissions_.size());
	for (const auto &profile : habitatProfiles_)
		permissions_.push_back(bool(habitats & profile.mask) && (!profile.growth || p.resourcesGrow) &&
							   (!profile.permanent || p.nonGrowingResources));
	const auto permitted = [&](unsigned resource)
	{ return permissions_[rule.habitatRow + resourceHabitatProfiles_[resource]] != 0; };

	// Corners that list their resources restrict the cell to resources all of them list.
	std::optional<std::vector<std::uint64_t>> listed;
	for (const auto type : c)
		if (const auto &list = typeAllowedResources_[type])
		{
			if (!listed) listed = *list;
			else
				for (std::size_t i = 0; i < listed->size(); ++i) (*listed)[i] &= (*list)[i];
		}
	const unsigned crop = p.farmMaterial;
	if (!listed)
	{
		for (std::size_t profile = 0; profile < habitatProfiles_.size(); ++profile)
			if (permissions_[rule.habitatRow + profile])
			{
				rule.materials |= habitatMaterials_[profile];
				if (crop < MaterialCount)
					rule.farmResource = std::min(rule.farmResource, habitatCrops_[profile][crop]);
			}
		return;
	}
	for (unsigned n = 0; n < resources_->size(); ++n)
	{
		auto &word = (*listed)[n / 64];
		const auto bit = std::uint64_t(1) << (n % 64);
		if (!(word & bit)) continue;
		if (!permitted(n))
		{
			word &= ~bit;
			continue;
		}
		const auto &resource = resources_->properties(static_cast<ResourceId>(n));
		rule.materials |= resource.materialMask;
		if (crop < MaterialCount && resource.farmable && (resource.materialMask & (1u << crop)))
			rule.farmResource = std::min(rule.farmResource, int(n));
	}
	auto shared = std::make_shared<const std::vector<std::uint64_t>>(*listed);
	rule.allowedResources = uniqueLists_.emplace(std::move(*listed), shared).first->second;
}
