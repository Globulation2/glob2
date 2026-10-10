// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ExperimentalFeatures.h"
#include "Material.h"
#include "UnitType.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using UnitTypeId = std::uint16_t;
inline constexpr unsigned BuiltinUnitCount = 3;

// Compiled simulation properties; authoring strings and production costs stay cold.
struct UnitRuntimeTraits
{
    enum Flag : std::uint32_t
    {
        Transport = 1u << 0,
        Construct = 1u << 1,
        Clear = 1u << 2,
        Melee = 1u << 3,
        GuardIdle = 1u << 4,
        ClearIdle = 1u << 5,
        Explore = 1u << 6,
        Convert = 1u << 7,
        CombatInterrupt = 1u << 8,
        MedicalIdle = 1u << 9,
        ExploreIdle = 1u << 10,
        ServiceRebound = 1u << 11,
        AdjacentClearInterrupt = 1u << 12,
        LearnConstruction = 1u << 13,
        Walk = 1u << 14,
        Swim = 1u << 15,
        Fly = 1u << 16,
        MagicAir = 1u << 17,
        MagicGround = 1u << 18,
        MagicCreateWood = 1u << 19,
        MagicCreateWheat = 1u << 20,
        MagicCreateAlga = 1u << 21,
        Regenerate = 1u << 22,
        SpillRejectedCargo = 1u << 23,
        ExtendedCargo = 1u << 24,
        // Derived only when importing historical Race tables. Legacy idle and
        // contact decisions inspect cached per-unit movement/attack values.
        LegacyPerformancePolicies = 1u << 25,
        CountsForSurvival = 1u << 26,
        ReleaseClearingClaims = 1u << 27
    };
    std::uint32_t flags = ReleaseClearingClaims;
    std::uint32_t learnableMask = 0;
    Sint32 foodCapacity = 150000, hungerRate = 425, starvationDamage = 1, regenerationQ8 = 0;
    Sint32 hungerTriggerNumerator = 1, hungerTriggerDenominator = 4;
    Sint32 carryingTriggerNumerator = 1, carryingTriggerDenominator = 10;
    Sint32 medicalTriggerNumerator = 3, medicalTriggerDenominator = 10;
    Sint32 idleHealMissingNumerator = 1, idleHealMissingDenominator = 10;
    Sint32 reboundNumerator = 9, reboundDenominator = 10;
    Sint32 feedingSpeedQ8 = 256, healingSpeedQ8 = 256, trainingSpeedQ8 = 256;
    Sint32 cargoCapacity = 1, cargoKinds = 1;
    Sint32 visionRadius = 1, magicRange = 3, attackSearchRadius = 8;
    Sint32 turretPriority = 2, flagRankingHealth = 200;
    Uint8 meshClass = 0;
    bool has(Flag flag) const { return (flags & flag) != 0; }
};
struct UnitDefinition
{
    std::string key, name, requiredExperiment;
    std::string sprite = "units", mesh = "worker";
    UnitRuntimeTraits runtime;
    std::array<UnitType, NB_UNIT_LEVELS> levels{};
    std::array<Sint32, MaterialSlotCount> cost{};
};

// Fully resolved immutable snapshots, shared by one game's teams and readers.
class UnitCatalog
{
  public:
    static constexpr unsigned Capacity = 1024;
    static constexpr std::size_t MaximumDefinitionBytes = 8 * 1024 * 1024;
    static std::shared_ptr<const UnitCatalog> builtins();
    static std::shared_ptr<const UnitCatalog> availableDefaults();
    static std::shared_ptr<const UnitCatalog> legacy();
    // Historical headers can predate the saved Race table entirely.
    static std::shared_ptr<const UnitCatalog> legacyMigration();
    static std::shared_ptr<const UnitCatalog> fromJson(std::string_view source);
    static std::shared_ptr<const UnitCatalog> deserialize(std::string_view source);
    static std::shared_ptr<const UnitCatalog> loadFile(const std::string &path);
    std::string serialize() const;
    std::size_t capacityBytes() const;
    std::size_t size() const { return definitions_.size(); }
    bool valid(unsigned id) const { return id < size(); }
    std::optional<UnitTypeId> find(std::string_view key) const;
    const UnitDefinition &definition(unsigned id) const;
    const UnitRuntimeTraits &runtime(unsigned id) const
    {
        assert(valid(id));
        return runtimeTable_[id];
    }
    const std::array<UnitType, NB_UNIT_LEVELS> &levels(unsigned id) const { return definition(id).levels; }
    const std::vector<CatalogExperimentDefinition> &experiments() const { return experiments_; }
    std::vector<std::string> experimentKeys() const;
    // Translate authoritative old race tables without touching another game.
    std::shared_ptr<const UnitCatalog>
    withLegacyLevels(const std::vector<std::array<UnitType, NB_UNIT_LEVELS>> &levels,
                     Sint32 hungerRate) const;
    std::uint32_t checksum() const { return checksum_; }
    const std::string &digest() const { return digest_; }

  private:
    UnitCatalog() = default;
    UnitCatalog(const UnitCatalog &) = default;
    UnitCatalog &operator=(const UnitCatalog &) = delete;
    std::vector<UnitDefinition> definitions_;
    std::vector<UnitRuntimeTraits> runtimeTable_;
    bool legacyLevelMovement_ = false;
    bool legacyPerformancePolicies_ = false;
    static std::shared_ptr<const UnitCatalog> parse(std::string_view source, bool snapshot);
    std::vector<CatalogExperimentDefinition> experiments_;
    std::uint32_t checksum_ = 0;
    std::string digest_;
    void compile();
};
