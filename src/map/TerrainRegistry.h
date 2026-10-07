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
	static constexpr std::size_t MaximumDefinitionBytes = 32 * 1024 * 1024;
	static constexpr std::size_t MaximumTextBytes = 128;

	struct Movement
	{
		std::vector<gradient_kernel::EntrySteps> entries;
		std::vector<gradient_kernel::EntrySteps> profiles;
		std::vector<std::uint8_t> profileIds;
		std::vector<unsigned> steps;
		using Prepared = std::variant<gradient_kernel::PreparedTerrainCosts<8>,
									  gradient_kernel::PreparedTerrainCosts<256>>;
		std::optional<Prepared> prepared;
		void prepare();
		unsigned minimum = GRADIENT_STEP;
	};
	// Factories publish only fully validated, compiled registries. Import creates
	// a replacement snapshot; readers may retain the previous one indefinitely.
	static std::shared_ptr<const TerrainRegistry> builtins();
	std::shared_ptr<const TerrainRegistry> importJson(std::string_view source) const;
	static std::shared_ptr<const TerrainRegistry> deserialize(std::string_view source);
	// Saved definitions are resolved and authoritative: no authoring inheritance
	// or local files are consulted during deserialization.
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
	// Saved-frame identity is independent of the current drawing catalog.
	const TerrainCompatibility &compatibility(TerrainType id) const
	{
		assert(valid(id));
		return savedPresentations_[id];
	}
	const std::string &key(TerrainType id) const { return keys_[id]; }
	TerrainType appearance(TerrainType id) const { return appearances_[id]; }
	const Movement &movement(unsigned swim) const { return movement_[swim]; }
	unsigned airCost(TerrainType id) const { return airCosts_[id]; }
	unsigned groundTravelCost(TerrainType id) const { return groundTravelCosts_[id]; }
	unsigned airRouteCost(TerrainType id) const { return airRouteCosts_[id]; }
	unsigned minimumAirCost() const { return minimumAirCost_; }
	bool swimming(TerrainType id) const { return properties_[id].swimmable; }
	std::uint32_t checksum() const { return checksum_; }
	const std::string &digest() const { return digest_; }

  private:
	TerrainRegistry();
	// Presentation strings borrow keys_/names_; only import may copy a registry,
	// and compile() repairs every borrowed pointer before the copy is published.
	TerrainRegistry(const TerrainRegistry &) = default;
	TerrainRegistry &operator=(const TerrainRegistry &) = delete;
	void compile();
	std::vector<TerrainProperties> properties_, propertyProfiles_;
	std::vector<std::uint16_t> propertyIndices_;
	std::vector<TerrainPresentation> presentations_;
	// Format 136 predates material catalogs. Retain these resolved fields verbatim
	// for saved definitions and their canonical digest; rendering uses appearance().
	struct SavedPresentation : TerrainCompatibility
	{
		int editorFrame;
		bool animatedBackdrop;
		int edgeFirstFrame = -1, layerPriority = 0;
		int animationFrames = 1, animationTicks = 1;
		int backdropFirstFrame = 0, backdropFrames = 1, backdropTicks = 1;
	};
	static SavedPresentation savedPreset(TerrainType appearance);
	std::vector<SavedPresentation> savedPresentations_;
	std::vector<std::string> keys_, names_;
	std::vector<TerrainType> appearances_;
	std::array<Movement, std::size(gradient_kernel::WATER_STEP)> movement_;
	std::vector<unsigned> airCosts_, airRouteCosts_, groundTravelCosts_;
	unsigned minimumAirCost_ = GRADIENT_STEP;
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
