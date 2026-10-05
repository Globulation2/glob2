// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainProperties.h"
#include "TerrainPresentation.h"
#include "field/TerrainMovementCosts.h"
#include "field/PreparedTerrainCosts.h"
#include <variant>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Immutable, map-owned definitions. Names and authoring metadata never enter a
// tile query. A scene/search retains ownership once, then borrows these arrays.
class TerrainRegistry
{
  public:
	static constexpr unsigned Capacity = 16384;
	struct Movement
	{
		std::vector<gradient_kernel::EntrySteps> entries;
		std::vector<gradient_kernel::EntrySteps> profiles;
		std::vector<std::uint8_t> profileIds;
		std::vector<unsigned> steps;
		using Prepared = std::variant<gradient_kernel::PreparedTerrainCosts<8>,
									  gradient_kernel::PreparedTerrainCosts<128>>;
		std::optional<Prepared> prepared;
		void prepare();
		unsigned minimum = GRADIENT_STEP;
	};
	static std::shared_ptr<const TerrainRegistry> builtins();
	std::shared_ptr<const TerrainRegistry> importJson(std::string_view source) const;
	static std::shared_ptr<const TerrainRegistry> deserialize(std::string_view source);
	std::string serialize() const;
	std::optional<TerrainType> find(std::string_view key) const;
	std::size_t size() const { return properties_.size(); }
	bool valid(unsigned id) const { return id < size(); }
	const TerrainProperties &properties(TerrainType id) const
	{
		assert(valid(id));
		return properties_[id];
	}
	// Simulation cells cache this compact index; canonical terrain IDs remain
	// unchanged for authoring, serialization and presentation.
	std::uint16_t propertyIndex(TerrainType id) const { return propertyIndices_[id]; }
	const std::vector<TerrainProperties> &propertyProfiles() const { return propertyProfiles_; }
	const TerrainPresentation &presentation(TerrainType id) const
	{
		assert(valid(id));
		return presentations_[id];
	}
	const std::string &key(TerrainType id) const { return keys_[id]; }
	TerrainType appearance(TerrainType id) const { return appearances_[id]; }
	const Movement &movement(unsigned swim) const { return movement_[swim]; }
	unsigned airCost(TerrainType id) const { return airCosts_[id]; }
	unsigned minimumAirCost() const { return minimumAirCost_; }
	int soleSwimmingType() const { return soleSwimmingType_; }
	bool swimming(TerrainType id) const { return properties_[id].swimmable; }
	std::uint32_t checksum() const { return checksum_; }
	const std::string &digest() const { return digest_; }

  private:
	TerrainRegistry();
	void compile();
	std::vector<TerrainProperties> properties_, propertyProfiles_;
	std::vector<std::uint16_t> propertyIndices_;
	std::vector<TerrainPresentation> presentations_;
	std::vector<std::string> keys_, names_;
	std::vector<TerrainType> appearances_;
	std::array<Movement, std::size(gradient_kernel::WATER_STEP)> movement_;
	std::vector<unsigned> airCosts_;
	unsigned minimumAirCost_ = GRADIENT_STEP;
	int soleSwimmingType_ = -1;
	std::uint32_t checksum_ = 0;
	std::string digest_;
};

// Cached with a map's terrain snapshot. Compact only the profiles actually used
// by its cells, so unused definitions cannot increase per-cost-layer setup.
struct TerrainMovementSnapshot
{
	TerrainRegistry::Movement movement;
	std::vector<std::uint8_t> cells;
	const std::uint8_t *data() const { return cells.data(); }
};
