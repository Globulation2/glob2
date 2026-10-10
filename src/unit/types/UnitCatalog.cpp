// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnitCatalog.h"
#include "UnitTiming.h"
#include "online/Sha256.h"
#include "map/TerrainProperties.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

#include "UnitCatalogDefaults.inc"

namespace
{
using Json = nlohmann::json;
const char *builtinKeys[] = {"worker", "explorer", "warrior"};
const char *recruitmentNames[] = {"recruitClear", "recruitExplore", "recruitDefend"};
const std::pair<const char *, UnitRuntimeTraits::Flag> flagNames[] = {
    {"transport", UnitRuntimeTraits::Transport},
    {"construct", UnitRuntimeTraits::Construct},
    {"clear", UnitRuntimeTraits::Clear},
    {"melee", UnitRuntimeTraits::Melee},
    {"guardIdle", UnitRuntimeTraits::GuardIdle},
    {"clearIdle", UnitRuntimeTraits::ClearIdle},
    {"explore", UnitRuntimeTraits::Explore},
    {"convert", UnitRuntimeTraits::Convert},
    {"combatInterrupt", UnitRuntimeTraits::CombatInterrupt},
    {"medicalIdle", UnitRuntimeTraits::MedicalIdle},
    {"exploreIdle", UnitRuntimeTraits::ExploreIdle},
    {"serviceRebound", UnitRuntimeTraits::ServiceRebound},
    {"adjacentClearInterrupt", UnitRuntimeTraits::AdjacentClearInterrupt},
    {"learnConstruction", UnitRuntimeTraits::LearnConstruction},
    {"walk", UnitRuntimeTraits::Walk},
    {"swim", UnitRuntimeTraits::Swim},
    {"fly", UnitRuntimeTraits::Fly},
    {"magicAir", UnitRuntimeTraits::MagicAir},
    {"magicGround", UnitRuntimeTraits::MagicGround},
    {"magicCreateWood", UnitRuntimeTraits::MagicCreateWood},
    {"magicCreateWheat", UnitRuntimeTraits::MagicCreateWheat},
    {"magicCreateAlga", UnitRuntimeTraits::MagicCreateAlga},
    {"spillRejectedCargo", UnitRuntimeTraits::SpillRejectedCargo},
    {"countsForSurvival", UnitRuntimeTraits::CountsForSurvival},
    {"releaseClearingClaims", UnitRuntimeTraits::ReleaseClearingClaims}};
// clang-format off
#define RUNTIME_FIELDS(X) \
    X(foodCapacity) \
    X(hungerRate) \
    X(starvationDamage) \
    X(regenerationQ8) \
    X(hungerTriggerNumerator) \
    X(hungerTriggerDenominator) \
    X(carryingTriggerNumerator) \
    X(carryingTriggerDenominator) \
    X(medicalTriggerNumerator) \
    X(medicalTriggerDenominator) \
    X(idleHealMissingNumerator) \
    X(idleHealMissingDenominator) \
    X(reboundNumerator) \
    X(reboundDenominator) \
    X(feedingSpeedQ8) \
    X(healingSpeedQ8) \
    X(trainingSpeedQ8) \
    X(cargoCapacity) \
    X(cargoKinds) \
    X(visionRadius) \
    X(magicRange) \
    X(attackSearchRadius) \
    X(turretPriority) \
    X(flagRankingHealth)
// clang-format on
class MissingCatalog : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
void fail(const std::string &message) { throw std::runtime_error("Unit catalog: " + message); }
void keys(const Json &j, std::initializer_list<const char *> allowed)
{
    if (!j.is_object())
        fail("expected object");
    for (auto i = j.begin(); i != j.end(); ++i)
        if (std::none_of(allowed.begin(), allowed.end(), [&](auto a) { return i.key() == a; }))
            fail("unknown field " + i.key());
}
Sint32 integer(const Json &j, Sint32 maximum = 1000000)
{
    if (!j.is_number_integer() || (j.is_number_unsigned() && j.get<std::uint64_t>() > std::uint64_t(maximum)))
        fail("integer out of range");
    const auto v = j.get<std::int64_t>();
    if (v < 0 || v > maximum)
        fail("integer out of range");
    return Sint32(v);
}
std::string text(const Json &j)
{
    if (!j.is_string())
        fail("expected string");
    const auto s = j.get<std::string>();
    if (s.empty() || s.size() > 128)
        fail("invalid text length");
    return s;
}
void key(const std::string &s)
{
    if (s.empty() || s.size() > 128 ||
        !std::all_of(s.begin(), s.end(),
                     [](unsigned char c)
                     {
                         return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == ':' || c == '.';
                     }))
        fail("invalid stable key " + s);
}
Json levelJson(const UnitType &l)
{
    return {{"startImage", l.startImage},
            {"hungriness", l.hungriness},
            {"performance", l.performance},
            {"harvestDamage", l.harvestDamage},
            {"armorReductionPerHappyness", l.armorReductionPerHappyness},
            {"experiencePerLevel", l.experiencePerLevel},
            {"magicActionCooldown", l.magicActionCooldown}};
}
void parseLevel(const Json &j, UnitType &l)
{
    keys(j, {"startImage", "hungriness", "performance", "harvestDamage", "armorReductionPerHappyness",
             "experiencePerLevel", "magicActionCooldown"});
    auto array = [&](const char *name, auto &out)
    {
        if (!j.contains(name))
            return;
        const auto &a = j.at(name);
        if (!a.is_array() || a.size() != std::size(out))
            fail(std::string(name) + " length");
        for (unsigned i = 0; i < a.size(); ++i)
            out[i] = integer(a[i]);
    };
    array("startImage", l.startImage);
    array("performance", l.performance);
    for (auto [name, value] : {std::pair{"hungriness", &l.hungriness},
                               {"harvestDamage", &l.harvestDamage},
                               {"armorReductionPerHappyness", &l.armorReductionPerHappyness},
                               {"experiencePerLevel", &l.experiencePerLevel},
                               {"magicActionCooldown", &l.magicActionCooldown}})
        if (j.contains(name))
            *value = integer(j.at(name));
}
Json runtimeJson(const UnitDefinition &definition)
{
    const auto &r = definition.runtime;
    Json j = Json::object();
    for (auto [name, flag] : flagNames)
        j[name] = r.has(flag);
#define WRITE(n) j[#n] = r.n;
    RUNTIME_FIELDS(WRITE)
#undef WRITE
    j["learnableMask"] = r.learnableMask;
    // Omit unspecified policies so pre-policy snapshots retain their canonical
    // bytes, digest, and simulation identity.
    for (unsigned role = 0; role < 3; ++role)
        if (definition.recruitmentOverrides[role].has_value())
            j[recruitmentNames[role]] = *definition.recruitmentOverrides[role];
    return j;
}
void parseRuntime(const Json &j, UnitDefinition &definition)
{
    auto &r = definition.runtime;
    if (!j.is_object())
        fail("behaviors must be object");
    std::set<std::string> allowed;
    for (unsigned role = 0; role < 3; ++role)
    {
        const auto *name = recruitmentNames[role];
        allowed.insert(name);
        if (j.contains(name))
        {
            if (!j.at(name).is_boolean())
                fail("recruitment policy must be boolean");
            definition.recruitmentOverrides[role] = j.at(name).get<bool>();
        }
    }
    for (auto [name, flag] : flagNames)
    {
        allowed.insert(name);
        if (j.contains(name))
        {
            if (!j.at(name).is_boolean())
                fail("capability must be boolean");
            if (j.at(name).get<bool>())
                r.flags |= flag;
            else
                r.flags &= ~flag;
        }
    }
#define READ(n)                                                                                              \
    allowed.insert(#n);                                                                                      \
    if (j.contains(#n))                                                                                      \
        r.n = integer(j.at(#n));
    RUNTIME_FIELDS(READ)
#undef READ
    allowed.insert("learnableMask");
    if (j.contains("learnableMask"))
        r.learnableMask = integer(j.at("learnableMask"), (1u << NB_ABILITY) - 1);
    for (auto i = j.begin(); i != j.end(); ++i)
        if (!allowed.contains(i.key()))
            fail("unknown behavior " + i.key());
}
} // namespace

std::shared_ptr<const UnitCatalog> UnitCatalog::legacy()
{
    static const auto result = []()
    {
        auto c = std::shared_ptr<UnitCatalog>(new UnitCatalog);
        for (unsigned i = 0; i < BuiltinUnitCount; ++i)
        {
            UnitDefinition d;
            d.key = d.name = builtinKeys[i];
            d.mesh = builtinKeys[i];
            d.runtime.meshClass = Uint8(i);
            d.runtime.turretPriority = i == WORKER ? 2 : (i == WARRIOR ? 3 : 4);
            std::copy_n(kDefaultUnitTypes[i], NB_UNIT_LEVELS, d.levels.begin());
            d.cost[materialIndex(MaterialId::Food)] = 5;
            d.runtime.flags = UnitRuntimeTraits::Convert | UnitRuntimeTraits::MedicalIdle |
                              UnitRuntimeTraits::SpillRejectedCargo;
            if (i != EXPLORER) d.runtime.flags |= UnitRuntimeTraits::CountsForSurvival;
            if (i == WORKER)
                d.runtime.flags |= UnitRuntimeTraits::Transport | UnitRuntimeTraits::Construct |
                                   UnitRuntimeTraits::Clear | UnitRuntimeTraits::ClearIdle |
                                   UnitRuntimeTraits::AdjacentClearInterrupt |
                                   UnitRuntimeTraits::LearnConstruction;
            if (i == EXPLORER)
            {
                d.runtime.flags |= UnitRuntimeTraits::Explore | UnitRuntimeTraits::ExploreIdle |
                                   UnitRuntimeTraits::ServiceRebound;
                d.runtime.visionRadius = 3;
            }
            if (i == WARRIOR)
            {
                d.runtime.flags |= UnitRuntimeTraits::Melee | UnitRuntimeTraits::GuardIdle |
                                   UnitRuntimeTraits::CombatInterrupt;
                d.runtime.hungerTriggerNumerator = 2;
                d.runtime.hungerTriggerDenominator = 10;
            }
            for (int a = 0; a < NB_ABILITY; ++a)
                if (d.levels.back().performance[a])
                    d.runtime.learnableMask |= 1u << a;
            for (auto [a, f] : {std::pair{WALK, UnitRuntimeTraits::Walk},
                                {SWIM, UnitRuntimeTraits::Swim},
                                {FLY, UnitRuntimeTraits::Fly},
                                {MAGIC_ATTACK_AIR, UnitRuntimeTraits::MagicAir},
                                {MAGIC_ATTACK_GROUND, UnitRuntimeTraits::MagicGround},
                                {MAGIC_CREATE_WOOD, UnitRuntimeTraits::MagicCreateWood},
                                {MAGIC_CREATE_WHEAT, UnitRuntimeTraits::MagicCreateWheat},
                                {MAGIC_CREATE_ALGA, UnitRuntimeTraits::MagicCreateAlga}})
                if (std::any_of(d.levels.begin(), d.levels.end(),
                                [&](const auto &l) { return l.performance[a] != 0; }))
                    d.runtime.flags |= f;
            c->definitions_.push_back(std::move(d));
        }
        c->compile();
        return c;
    }();
    return result;
}
std::shared_ptr<const UnitCatalog> UnitCatalog::builtins()
{
    static const auto result = loadFile("data/units/registry.json");
    return result;
}
std::shared_ptr<const UnitCatalog> UnitCatalog::legacyMigration()
{
    static const auto result = []()
    {
        const auto base = legacy();
        std::vector<std::array<UnitType, NB_UNIT_LEVELS>> tables;
        tables.reserve(base->size());
        for (unsigned type = 0; type < base->size(); ++type)
            tables.push_back(base->levels(type));
        return base->withLegacyLevels(tables, 425);
    }();
    return result;
}
std::shared_ptr<const UnitCatalog> UnitCatalog::availableDefaults()
{
    try
    {
        return builtins();
    }
    catch (const MissingCatalog &)
    {
        return legacy();
    }
}
const UnitDefinition &UnitCatalog::definition(unsigned id) const
{
    assert(valid(id));
    return definitions_[id];
}
std::optional<UnitTypeId> UnitCatalog::find(std::string_view key) const
{
    for (unsigned i = 0; i < size(); ++i)
        if (definitions_[i].key == key)
            return UnitTypeId(i);
    return std::nullopt;
}
std::vector<std::string> UnitCatalog::experimentKeys() const
{
    std::vector<std::string> r;
    for (const auto &e : experiments_)
        r.push_back(e.key);
    return r;
}
std::string UnitCatalog::serialize() const
{
    Json j = {{"schemaVersion", 1}, {"units", Json::array()}, {"experiments", Json::array()}};
    if (legacyLevelMovement_)
        j["legacyLevelMovement"] = true;
    if (legacyPerformancePolicies_)
        j["legacyPerformancePolicies"] = true;
    for (const auto &d : definitions_)
    {
        Json cost = Json::object();
        for (unsigned i = 0; i < MaterialCount; ++i)
            if (d.cost[i])
                cost[MaterialKeys[i]] = d.cost[i];
        Json levels = Json::array();
        for (const auto &l : d.levels)
            levels.push_back(levelJson(l));
        j["units"].push_back({{"key", d.key},
                              {"name", d.name},
                              {"requiredExperiment", d.requiredExperiment},
                              {"sprite", d.sprite},
                              {"mesh", d.mesh},
                              {"behaviors", runtimeJson(d)},
                              {"levels", levels},
                              {"cost", cost}});
    }
    for (const auto &e : experiments_)
        j["experiments"].push_back({{"key", e.key}, {"label", e.label}, {"help", e.help}});
    return j.dump();
}
std::size_t UnitCatalog::capacityBytes() const
{
    std::size_t total = definitions_.capacity() * sizeof(UnitDefinition) +
                        runtimeTable_.capacity() * sizeof(UnitRuntimeTraits) +
                        experiments_.capacity() * sizeof(CatalogExperimentDefinition);
    const auto stringBytes = [](const std::string &value)
    {
        const auto object = reinterpret_cast<std::uintptr_t>(&value);
        const auto storage = reinterpret_cast<std::uintptr_t>(value.data());
        return storage >= object && storage < object + sizeof(value) ? std::size_t{0} : value.capacity() + 1;
    };
    for (const auto &definition : definitions_)
        for (const auto *value : {&definition.key, &definition.name, &definition.requiredExperiment,
                                  &definition.sprite, &definition.mesh})
            total += stringBytes(*value);
    for (const auto &experiment : experiments_)
        for (const auto *value : {&experiment.key, &experiment.label, &experiment.help})
            total += stringBytes(*value);
    return total + stringBytes(digest_);
}
std::shared_ptr<const UnitCatalog> UnitCatalog::fromJson(std::string_view source)
{
    return parse(source, false);
}
std::shared_ptr<const UnitCatalog> UnitCatalog::parse(std::string_view source, bool snapshot)
{
    if (source.size() > MaximumDefinitionBytes)
        fail("definition too large");
    Json j = Json::parse(source);
    if (snapshot)
        keys(j, {"schemaVersion", "units", "experiments", "legacyLevelMovement", "legacyPerformancePolicies"});
    else
        keys(j, {"schemaVersion", "units", "experiments"});
    if (!j.contains("schemaVersion") || integer(j.at("schemaVersion")) != 1)
        fail("unsupported schema");
    auto c = std::shared_ptr<UnitCatalog>(new UnitCatalog(*legacy()));
    if (j.contains("legacyLevelMovement"))
    {
        if (!j.at("legacyLevelMovement").is_boolean())
            fail("invalid legacy movement marker");
        c->legacyLevelMovement_ = j.at("legacyLevelMovement").get<bool>();
    }
    if (j.contains("legacyPerformancePolicies"))
    {
        if (!j.at("legacyPerformancePolicies").is_boolean())
            fail("invalid legacy performance policy marker");
        c->legacyPerformancePolicies_ = j.at("legacyPerformancePolicies").get<bool>();
    }
    if (j.contains("experiments"))
    {
        c->experiments_.clear();
        if (!j.at("experiments").is_array() || j.at("experiments").size() > ExperimentSet::MAX_STORED)
            fail("invalid experiments");
        for (const auto &e : j.at("experiments"))
        {
            keys(e, {"key", "label", "help"});
            c->experiments_.push_back({text(e.at("key")), text(e.at("label")), text(e.at("help"))});
        }
    }
    if (!j.contains("units") || !j.at("units").is_array() || j.at("units").size() > Capacity)
        fail("invalid units");
    std::set<std::string> seen;
    for (const auto &entry : j.at("units"))
    {
        keys(entry, {"key", "extends", "name", "requiredExperiment", "sprite", "mesh", "behaviors", "levels",
                     "cost"});
        if (snapshot)
        {
            for (const auto *field :
                 {"key", "name", "requiredExperiment", "sprite", "mesh", "behaviors", "levels", "cost"})
                if (!entry.contains(field))
                    fail("incomplete resolved unit");
            if (entry.contains("extends"))
                fail("snapshot contains unresolved inheritance");
            const auto &behaviors = entry.at("behaviors");
            for (auto [name, flag] : flagNames)
                if (!behaviors.contains(name))
                    fail("incomplete resolved behaviors");
#define REQUIRED(n)                                                                                          \
    if (!behaviors.contains(#n))                                                                             \
        fail("incomplete resolved behaviors");
            RUNTIME_FIELDS(REQUIRED)
#undef REQUIRED
            if (!behaviors.contains("learnableMask"))
                fail("incomplete resolved behaviors");
            for (const auto &level : entry.at("levels"))
                for (const auto *field :
                     {"startImage", "hungriness", "performance", "harvestDamage",
                      "armorReductionPerHappyness", "experiencePerLevel", "magicActionCooldown"})
                    if (!level.contains(field))
                        fail("incomplete resolved level");
        }
        const auto k = text(entry.at("key"));
        if (!seen.insert(k).second)
            fail("duplicate unit key");
        UnitDefinition d;
        auto previous = c->find(k);
        if (previous)
            d = c->definition(*previous);
        if (entry.contains("extends"))
        {
            auto base = c->find(text(entry.at("extends")));
            if (!base)
                fail("unknown inheritance key");
            d = c->definition(*base);
        }
        // New types default to releasing claims even when their ability tables
        // extend a stock definition with historical claim behavior. An explicit
        // authored false value can retain that policy.
        if (!previous && !snapshot) d.runtime.flags |= UnitRuntimeTraits::ReleaseClearingClaims;
        d.key = k;
        d.name = entry.value("name", k);
        d.requiredExperiment = entry.value("requiredExperiment", d.requiredExperiment);
        d.sprite = entry.value("sprite", d.sprite);
        d.mesh = entry.value("mesh", d.mesh);
        if (entry.contains("behaviors"))
            parseRuntime(entry.at("behaviors"), d);
        if (entry.contains("levels"))
        {
            const auto &a = entry.at("levels");
            if (!a.is_array() || a.size() != NB_UNIT_LEVELS)
                fail("expected four levels");
            for (unsigned i = 0; i < NB_UNIT_LEVELS; ++i)
                parseLevel(a[i], d.levels[i]);
        }
        if (entry.contains("cost"))
        {
            d.cost = {};
            if (!entry.at("cost").is_object())
                fail("cost must be object");
            for (auto i = entry.at("cost").begin(); i != entry.at("cost").end(); ++i)
            {
                auto m = parseMaterialKey(i.key());
                if (!m)
                    fail("unknown material");
                d.cost[materialIndex(*m)] = integer(i.value());
            }
        }
        if (previous)
            c->definitions_[*previous] = std::move(d);
        else
            c->definitions_.push_back(std::move(d));
    }
    c->compile();
    return c;
}
std::shared_ptr<const UnitCatalog> UnitCatalog::deserialize(std::string_view source)
{
    auto result = parse(source, true);
    auto j = Json::parse(source);
    for (const auto &entry : j.at("units"))
        if (entry.contains("extends"))
            fail("snapshot contains unresolved inheritance");
    if (j.at("units").size() != result->size())
        fail("incomplete resolved snapshot");
    return result;
}
std::shared_ptr<const UnitCatalog> UnitCatalog::loadFile(const std::string &path)
{
    auto *manager = GAGCore::Toolkit::getFileManager();
    struct Close
    {
        void operator()(FILE *f) const { std::fclose(f); }
    };
    std::unique_ptr<FILE, Close> file(manager ? manager->openFP(path, "rb") : std::fopen(path.c_str(), "rb"));
    if (!file)
    {
        if (errno == ENOENT)
            throw MissingCatalog("Unit catalog is not installed: " + path);
        fail("cannot open " + path);
    }
    std::string data;
    std::array<char, 4096> b{};
    for (;;)
    {
        const auto n = std::fread(b.data(), 1, b.size(), file.get());
        data.append(b.data(), n);
        if (data.size() > MaximumDefinitionBytes)
            fail("definition too large");
        if (n < b.size())
        {
            if (std::ferror(file.get()))
                fail("read failed");
            break;
        }
    }
    return fromJson(data);
}
std::shared_ptr<const UnitCatalog>
UnitCatalog::withLegacyLevels(const std::vector<std::array<UnitType, NB_UNIT_LEVELS>> &levels,
                              Sint32 hungerRate) const
{
    if (levels.size() != size())
        fail("legacy table count mismatch");
    auto c = std::shared_ptr<UnitCatalog>(new UnitCatalog(*this));
    if (hungerRate >= 0)
        c->legacyPerformancePolicies_ = true;
    if (hungerRate >= 0)
        c->legacyLevelMovement_ =
            std::any_of(levels.begin(), levels.end(),
                        [](const auto &table)
                        {
                            const bool first = table[0].performance[FLY] != 0;
                            return std::any_of(table.begin(), table.end(), [&](const auto &level)
                                               { return (level.performance[FLY] != 0) != first; });
                        });
    for (unsigned i = 0; i < size(); ++i)
    {
        c->definitions_[i].levels = levels[i];
        if (hungerRate >= 0)
        {
            c->definitions_[i].runtime.hungerRate = hungerRate;
            c->definitions_[i].runtime.flagRankingHealth = levels[WORKER][0].performance[HP];
            auto &runtime = c->definitions_[i].runtime;
            runtime.learnableMask = 0;
            for (int ability = 0; ability < NB_ABILITY; ++ability)
                if (levels[i].back().performance[ability])
                    runtime.learnableMask |= 1u << ability;
            for (auto [ability, flag] : {std::pair{WALK, UnitRuntimeTraits::Walk},
                                         {SWIM, UnitRuntimeTraits::Swim},
                                         {FLY, UnitRuntimeTraits::Fly},
                                         {ATTACK_SPEED, UnitRuntimeTraits::Melee},
                                         {MAGIC_ATTACK_AIR, UnitRuntimeTraits::MagicAir},
                                         {MAGIC_ATTACK_GROUND, UnitRuntimeTraits::MagicGround},
                                         {MAGIC_CREATE_WOOD, UnitRuntimeTraits::MagicCreateWood},
                                         {MAGIC_CREATE_WHEAT, UnitRuntimeTraits::MagicCreateWheat},
                                         {MAGIC_CREATE_ALGA, UnitRuntimeTraits::MagicCreateAlga}})
            {
                runtime.flags &= ~flag;
                if (std::any_of(levels[i].begin(), levels[i].end(),
                                [&](const auto &level) { return level.performance[ability] != 0; }))
                    runtime.flags |= flag;
            }
            runtime.hungerTriggerNumerator = levels[i][0].performance[ATTACK_SPEED] ? 2 : 1;
            runtime.hungerTriggerDenominator = levels[i][0].performance[ATTACK_SPEED] ? 10 : 4;
        }
    }
    c->compile();
    return c;
}
void UnitCatalog::compile()
{
    if (size() < BuiltinUnitCount || size() > Capacity)
        fail("type count out of range");
    validateCatalogExperiments(experiments_);
    std::set<std::string> seen;
    for (unsigned i = 0; i < size(); ++i)
    {
        auto &d = definitions_[i];
        key(d.key);
        if (!seen.insert(d.key).second)
            fail("duplicate key");
        if (i < BuiltinUnitCount && d.key != builtinKeys[i])
            fail("builtin IDs changed");
        if (d.name.empty() || d.name.size() > 128 || d.sprite.empty() || d.sprite.size() > 128)
            fail("invalid presentation");
        if (d.sprite != "units" && d.sprite != "data/gfx/unit")
            fail("unsupported unit sprite atlas");
        if (d.mesh == "worker")
            d.runtime.meshClass = WORKER;
        else if (d.mesh == "explorer")
            d.runtime.meshClass = EXPLORER;
        else if (d.mesh == "warrior")
            d.runtime.meshClass = WARRIOR;
        else
            fail("unknown mesh");
        if (!d.requiredExperiment.empty() &&
            std::none_of(experiments_.begin(), experiments_.end(),
                         [&](const auto &e) { return e.key == d.requiredExperiment; }))
            fail("unresolved experiment");
        auto &r = d.runtime;
        const bool roleCapabilities[] = {
            r.has(UnitRuntimeTraits::Clear), r.has(UnitRuntimeTraits::Explore),
            r.has(UnitRuntimeTraits::Melee) || r.has(UnitRuntimeTraits::GuardIdle)};
        r.recruitmentMask = 0;
        for (unsigned role = 0; role < 3; ++role)
            if (roleCapabilities[role] && d.recruitmentOverrides[role].value_or(true))
                r.recruitmentMask |= Uint8(1u << role);
        if (legacyPerformancePolicies_ && i < BuiltinUnitCount)
            r.flags |= UnitRuntimeTraits::LegacyPerformancePolicies;
        else
            r.flags &= ~UnitRuntimeTraits::LegacyPerformancePolicies;
        if (r.regenerationQ8)
            r.flags |= UnitRuntimeTraits::Regenerate;
        else
            r.flags &= ~UnitRuntimeTraits::Regenerate;
        if (r.cargoCapacity != 1 || !r.has(UnitRuntimeTraits::SpillRejectedCargo))
            r.flags |= UnitRuntimeTraits::ExtendedCargo;
        else
            r.flags &= ~UnitRuntimeTraits::ExtendedCargo;
        for (auto [n, q] : {std::pair{r.hungerTriggerNumerator, r.hungerTriggerDenominator},
                            {r.carryingTriggerNumerator, r.carryingTriggerDenominator},
                            {r.medicalTriggerNumerator, r.medicalTriggerDenominator},
                            {r.idleHealMissingNumerator, r.idleHealMissingDenominator},
                            {r.reboundNumerator, r.reboundDenominator}})
            if (q <= 0 || n < 0 || n > q || q > 1000)
                fail("invalid threshold ratio");
        if (r.foodCapacity <= 0 || r.foodCapacity > 1000000 || r.hungerRate < 0 ||
            r.hungerRate > r.foodCapacity || r.starvationDamage < 0 || r.starvationDamage > 1000000 ||
            r.regenerationQ8 < 0 || r.regenerationQ8 > 65536)
            fail("invalid medical properties");
        if (r.cargoCapacity < 0 || r.cargoCapacity > 65535 || r.cargoKinds < 0 ||
            r.cargoKinds > Sint32(MaterialCount) || r.cargoKinds > r.cargoCapacity ||
            (r.has(UnitRuntimeTraits::Transport) && (!r.cargoCapacity || !r.cargoKinds)))
            fail("invalid cargo limits");
        if (r.feedingSpeedQ8 <= 0 || r.healingSpeedQ8 <= 0 || r.trainingSpeedQ8 <= 0 ||
            r.feedingSpeedQ8 > 65536 || r.healingSpeedQ8 > 65536 || r.trainingSpeedQ8 > 65536)
            fail("invalid service speed");
        const int minimumLegacyHealth=r.has(UnitRuntimeTraits::LegacyPerformancePolicies) ? 0 : 1;
        if (r.visionRadius < 0 || r.visionRadius > 32 || r.magicRange < 0 || r.magicRange > 32 ||
            r.attackSearchRadius < 0 || r.attackSearchRadius > 64 || r.turretPriority < 2 ||
            r.turretPriority > 4 || r.flagRankingHealth < minimumLegacyHealth || r.flagRankingHealth > 1000000)
            fail("invalid search radius");
        const bool fly = d.levels[0].performance[FLY] != 0;
        for (const auto &l : d.levels)
        {
            if (l.performance[HP] < minimumLegacyHealth || l.performance[HP] > 1000000)
                fail("invalid HP");
            if (!(legacyLevelMovement_ && i < BuiltinUnitCount) && r.has(UnitRuntimeTraits::Fly) &&
                ((!fly) || ((l.performance[FLY] != 0) != fly)))
                fail("flight mode changes across levels");
            for (int a = 0; a < NB_ABILITY; ++a)
                if (l.performance[a] < 0 || l.performance[a] > 1000000)
                    fail("invalid ability");
            for (int a : {STOP_WALK, STOP_SWIM, STOP_FLY, WALK, SWIM, FLY, BUILD, HARVEST, ATTACK_SPEED})
                if (l.performance[a] > UNIT_DELTA_MAX)
                    fail("action speed exceeds one tile per tick");
            if (l.harvestDamage < 0 || l.magicActionCooldown < 0 || l.magicActionCooldown > 1000000 ||
                l.experiencePerLevel < 0 || l.experiencePerLevel > 1000000 ||
                l.armorReductionPerHappyness < 0 || l.armorReductionPerHappyness > 1000000)
                fail("invalid level properties");
        }
        for (unsigned m = 0; m < MaterialSlotCount; ++m)
            if (d.cost[m] < 0 || d.cost[m] > 1000000 || (m >= MaterialCount && d.cost[m]))
                fail("invalid unit cost");
    }
    runtimeTable_.clear();
    runtimeTable_.reserve(size());
    for (const auto &d : definitions_)
        runtimeTable_.push_back(d.runtime);
    const auto snapshot = serialize();
    if (snapshot.size() > MaximumDefinitionBytes)
        fail("snapshot too large");
    const auto hash = Online::Sha256::digest(snapshot);
    digest_ = Online::Sha256::toHex(hash);
    checksum_ = std::uint32_t(hash[0]) | std::uint32_t(hash[1]) << 8 | std::uint32_t(hash[2]) << 16 |
                std::uint32_t(hash[3]) << 24;
}
