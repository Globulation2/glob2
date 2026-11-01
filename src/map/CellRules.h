// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainRegistry.h"
#include "ResourceRegistry.h"
#include "Ressource.h"
#include "field/TerrainMovementCosts.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// Terrain is stored once per map vertex; vertex (x,y) is the top-left corner
// of cell (x,y). A cell's gameplay rules are derived from its four corners by
// combineCornerRules, and every distinct corner pattern is compiled once
// into a CellRule: the combined properties, its movement costs and its
// resource habitat. Cells store only the index of their rule.
struct CellRule
{
	// The four corners in canonical rotation/reflection order: the rule's identity.
	std::array<TerrainType, 4> corners{};
	TerrainProperties properties;
	// Movement preserves connected shore edges independently of ecology/placement.
	std::uint8_t groundAccess = 0; // walk, amphibious, water-only
	bool usesSwimming = false;
	unsigned movementSpeedQ8 = 256, swimmingSpeedQ8 = 256;
	bool allowsMovement(int movement) const
	{
		return movement == AIR_MOVEMENT_CLASS
				   ? properties.flyable
				   : (groundAccess & (1u << (movement == WATER_ONLY_CLASS ? 2
											 : movement > 0               ? 1
																		  : 0)));
	}
	// Damage-weighted movement costs; the historical name includes the air class.
	std::array<gradient_kernel::EntrySteps, MOVEMENT_CLASS_COUNT> ground{};
	unsigned airCost = GRADIENT_STEP, airRouteCost = GRADIENT_STEP, groundTravelCost = GRADIENT_STEP;
	// Resource habitat: a permission per resource habitat profile, unless some
	// corner terrain names its resources explicitly; then only the resources
	// every such corner names, and the combined rules permit, may grow here.
	std::uint32_t habitatRow = 0;
	std::shared_ptr<const std::vector<std::uint64_t>> allowedResources;
	MaterialMask materials = 0;
	int farmResource = NO_RES_TYPE;

	bool uniform() const
	{
		return corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3];
	}
};

// Integer wire table compiled with the ordinary corner rules. All movement classes
// use the same representation; no map-sized host preparation is needed.
struct GradientPreparationRule
{
	std::uint32_t accessibility = 0;
	std::array<std::uint32_t, MOVEMENT_CLASS_COUNT> costs{};
};
static_assert(sizeof(GradientPreparationRule) == 40);

// The compiled rules of one map, for one terrain and one resource registry.
// Rule t is the uniform cell of terrain type t, for every registered type; mixed
// combinations follow in the order the map first needs them, so an index is
// only meaningful with the table that issued it. Tables are shared immutably
// with engine snapshots, so Map clones a table before adding to one it shares.
class CellRuleTable
{
  public:
	static constexpr std::size_t Capacity = 65536;
	using Key = std::array<TerrainType, 4>;
	// Access has four variants while movement costs have nine classes.
	// CPU seed loops use one narrow column; the accelerator wire table retains
	// its all-class mask and costs. Both are compiled from the same rule.
	inline static constexpr std::array<int, 4> OpenTerrainClasses{0, 1, WATER_ONLY_CLASS, AIR_MOVEMENT_CLASS};

	CellRuleTable(std::shared_ptr<const TerrainRegistry> terrains,
				  std::shared_ptr<const ResourceRegistry> resources);

	static Key key(TerrainType a, TerrainType b, TerrainType c, TerrainType d);
	std::optional<std::uint16_t> find(const Key &key) const;
	// Throws std::length_error once Capacity distinct combinations exist.
	std::uint16_t intern(const Key &key);
	std::uint16_t intern(TerrainType a, TerrainType b, TerrainType c, TerrainType d)
	{
		return intern(key(a, b, c, d));
	}

	std::size_t size() const { return rules_.size(); }
	const CellRule &operator[](std::uint16_t rule) const { return rules_[rule]; }
	const CellRule *data() const { return rules_.data(); }
	const std::vector<GradientPreparationRule>& gradientPreparation() const { return gradientPreparation_; }
	// Pass swimAccessVariant(swim), rather than the movement cost class.
	const std::vector<std::uint16_t> &openTerrainSeeds(unsigned accessVariant) const
	{
		return openTerrainSeeds_.at(accessVariant);
	}
	const TerrainRegistry &terrains() const { return *terrains_; }
	const ResourceRegistry &resources() const { return *resources_; }
	const std::shared_ptr<const TerrainRegistry> &terrainRegistry() const { return terrains_; }
	const std::shared_ptr<const ResourceRegistry> &resourceRegistry() const { return resources_; }

	bool supportsResource(std::uint16_t rule, unsigned resourceType) const
	{
		if (resourceType >= resourceHabitatProfiles_.size()) return false;
		const auto &r = rules_[rule];
		if (r.allowedResources)
			return ((*r.allowedResources)[resourceType / 64] >> (resourceType % 64)) & 1u;
		return permissions_[r.habitatRow + resourceHabitatProfiles_[resourceType]];
	}
	// The cost interface of TerrainRegistry, keyed by rule, for the field kernels.
	const TerrainProperties &properties(std::uint16_t rule) const { return rules_[rule].properties; }
	bool swimming(std::uint16_t rule) const { return rules_[rule].usesSwimming; }
	unsigned airCost(std::uint16_t rule) const { return rules_[rule].airCost; }
	unsigned airRouteCost(std::uint16_t rule) const { return rules_[rule].airRouteCost; }
	unsigned groundTravelCost(std::uint16_t rule) const { return rules_[rule].groundTravelCost; }
	const TerrainRegistry::Movement &movement(unsigned swim) const { return movement_[swim]; }

	MaterialMask materialPermissions(std::uint16_t rule) const { return rules_[rule].materials; }
	std::size_t capacityBytes() const
	{
		std::size_t bytes = rules_.capacity() * sizeof(CellRule) + permissions_.capacity()
			+ gradientPreparation_.capacity() * sizeof(GradientPreparationRule);
		for (const auto &column : openTerrainSeeds_) bytes += column.capacity() * sizeof(std::uint16_t);
		return bytes;
	}
	int farmResource(std::uint16_t rule) const { return rules_[rule].farmResource; }

  private:
	struct HabitatProfile
	{
		unsigned mask;
		bool growth, permanent;
		bool operator==(const HabitatProfile &) const = default;
	};
	void compile(CellRule &rule);
	std::uint16_t add(const Key &key, bool prepare);
	std::array<TerrainRegistry::Movement, MOVEMENT_CLASS_COUNT> movement_;
	std::shared_ptr<const TerrainRegistry> terrains_;
	std::shared_ptr<const ResourceRegistry> resources_;
	std::vector<CellRule> rules_;
	std::vector<GradientPreparationRule> gradientPreparation_;
	std::array<std::vector<std::uint16_t>, OpenTerrainClasses.size()> openTerrainSeeds_;
	std::map<Key, std::uint16_t> index_;
	// Per resource registry: each resource's habitat profile, and per profile
	// the materials it supplies and its first farmable resource per material.
	std::vector<std::uint16_t> resourceHabitatProfiles_;
	std::vector<HabitatProfile> habitatProfiles_;
	std::vector<MaterialMask> habitatMaterials_;
	std::vector<std::array<int, MaterialCount>> habitatCrops_;
	// Per terrain type: its explicit resource list as a bit set, or null.
	std::vector<std::shared_ptr<const std::vector<std::uint64_t>>> typeAllowedResources_;
	std::vector<std::uint8_t> permissions_;
	std::map<std::vector<std::uint64_t>, std::shared_ptr<const std::vector<std::uint64_t>>> uniqueLists_;
};
