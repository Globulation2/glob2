// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceRegistry.h"
#include "online/Sha256.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
using Json = nlohmann::json;
[[noreturn]] void fail(const std::string& context, const std::string& message)
{
    throw std::invalid_argument("Resource catalog " + context + ": " + message);
}
void fields(const Json& value, std::initializer_list<const char*> allowed, const std::string& context)
{
    if (!value.is_object()) fail(context, "must be an object");
    for (const auto& item : value.items())
        if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
            fail(context, "unknown field " + item.key());
}
std::string text(const Json& value, const char* field, const std::string& context, std::size_t maximum = 128)
{
    const auto& member = value.at(field);
    if (!member.is_string()) fail(context, std::string(field) + " must be text");
    auto result = member.get<std::string>();
    if (result.empty() || result.size() > maximum || result.find('\0') != std::string::npos)
        fail(context, std::string(field) + " is empty or too long");
    return result;
}
void keyValid(const std::string& key)
{
    if (key.empty() || key.size() > 128) fail(key, "invalid key length");
    unsigned colons = 0;
    for (const unsigned char c : key)
    {
        if (c == ':') ++colons;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '-' || c == '_' || c == '.'))
            fail(key, "keys use lowercase ASCII letters, digits, colon, dot, dash or underscore");
    }
    if (colons > 1 || key.front() == ':' || key.back() == ':') fail(key, "invalid key namespace");
}
template<class T> void property(const Json& value, const char* field, T& out, const std::string& context)
{
    const auto found = value.find(field);
    if (found == value.end()) return;
    if constexpr (std::is_same_v<T, bool>)
    {
        if (!found->is_boolean()) fail(context, std::string(field) + " must be boolean");
        out = found->get<bool>();
    }
    else
    {
        if (!found->is_number_integer() ||
            (found->is_number_unsigned() && found->get<std::uint64_t>() > std::uint64_t(std::numeric_limits<T>::max())))
            fail(context, std::string(field) + " must be an in-range integer");
        const auto n = found->get<std::int64_t>();
        if (n < 0 || std::uint64_t(n) > std::uint64_t(std::numeric_limits<T>::max()))
            fail(context, std::string(field) + " is out of range");
        out = static_cast<T>(n);
    }
}
Json parse(std::string_view source)
{
    if (source.size() > ResourceRegistry::MaximumDefinitionBytes) fail("JSON", "exceeds 32 MiB");
    std::vector<std::set<std::string>> objects;
    auto result = Json::parse(source, [&objects](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 16) fail("JSON", "nesting limit exceeded");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        else if (event == Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
            fail("JSON", "duplicate object field " + value.get<std::string>());
        else if (event == Json::parse_event_t::object_end) objects.pop_back();
        return true;
    });
    fields(result, {"schemaVersion", "resources", "experiments"}, "JSON");
    if (!result.at("schemaVersion").is_number_integer() || result.at("schemaVersion") != 1)
        fail("JSON", "unsupported schemaVersion");
    if (!result.at("resources").is_array() || result.at("resources").size() > ResourceRegistry::Capacity)
        fail("JSON", "invalid resource array");
    return result;
}

#define RESOURCE_FIELDS(X) \
    X(growthRate) X(spreadRate) X(habitatMask) X(stockBranchDivisor) \
    X(blocksGround) X(blocksAir) X(blocksBuilding) X(clearable) X(visibleToHarvest) \
    X(persistsWhenEmpty) X(farmable) X(stockDependentGrowth) X(smoothPlacement) \
    X(requiresGrowthTerrain) X(requiresPermanentDepositsTerrain)
#define YIELD_FIELDS(X) X(capacity) X(initial) X(seedReserve) X(placementMaximum) X(growthRate) X(destroysDeposit)

constexpr const char* EcologyNames[] = {"none", "land", "shore", "uniform"};
constexpr const char* ConsumptionNames[] = {"one", "all", "infinite"};
template<class Enum, std::size_t N> Enum named(const std::string& name, const char* const (&names)[N], const std::string& context)
{
    for (unsigned i = 0; i < N; ++i) if (name == names[i]) return static_cast<Enum>(i);
    fail(context, "unknown value " + name);
}
void spritePath(const std::string& path, const std::string& context)
{
    if (!path.starts_with("data/") || path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
        fail(context, "sprite must be a data/-relative prefix");
    std::size_t begin = 0;
    do
    {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end == std::string::npos ? end : end - begin);
        if (component.empty() || component == "." || component == "..") fail(context, "invalid sprite path component");
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
}
ResourcePresentation readPresentation(const Json& value, const std::string& context)
{
    fields(value, {"name", "sprite", "minimap", "levels", "animationFrames", "animationStride", "animationTicks"}, context);
    ResourcePresentation result;
    result.name = text(value, "name", context, 512);
    result.sprite = text(value, "sprite", context, 512);
    spritePath(result.sprite, context);
    property(value, "animationFrames", result.animationFrames, context);
    property(value, "animationStride", result.animationStride, context);
    property(value, "animationTicks", result.animationTicks, context);
    if (!result.animationFrames || result.animationFrames > 256 || !result.animationStride || !result.animationTicks)
        fail(context, "invalid animation dimensions");
    const auto& color = value.at("minimap");
    if (!color.is_array() || color.size() != 3) fail(context, "minimap needs three channels");
    for (unsigned i = 0; i < 3; ++i)
    {
        const Json channel = {{"channel", color[i]}};
        property(channel, "channel", result.minimap[i], context);
    }
    const auto& levels = value.at("levels");
    if (!levels.is_array() || levels.empty() || levels.size() > 4096) fail(context, "invalid sprite levels");
    for (const auto& level : levels)
    {
        fields(level, {"stock", "variants"}, context);
        ResourceSpriteLevel compiled;
        if (!level.contains("stock")) fail(context, "level requires stock threshold");
        property(level, "stock", compiled.stock, context);
        if ((result.levels.empty() && compiled.stock != 0) ||
            (!result.levels.empty() && compiled.stock <= result.levels.back().stock))
            fail(context, "sprite thresholds must start at zero and strictly increase");
        const auto& variants = level.at("variants");
        if (!variants.is_array() || variants.empty() || variants.size() > 256) fail(context, "invalid sprite variants");
        std::uint64_t total = 0;
        for (const auto& variant : variants)
        {
            fields(variant, {"frame", "weight"}, context);
            if (!variant.contains("frame")) fail(context, "variant requires frame");
            ResourceSpriteVariant item;
            property(variant, "frame", item.frame, context);
            property(variant, "weight", item.weight, context);
            total += item.weight;
            if (!item.weight || total > 1000000000 ||
                std::uint64_t(item.frame) + std::uint64_t(result.animationFrames - 1) * result.animationStride > 65535)
                fail(context, "invalid sprite weight or animation frame range");
            compiled.variants.push_back(item);
        }
        result.levels.push_back(std::move(compiled));
    }
    return result;
}
struct Definition
{
    std::string key, experiment;
    ResourceProperties properties;
    ResourceYields yields;
    ResourcePresentation presentation;
};
Definition readDefinition(const Json& value)
{
    fields(value, {"key", "properties", "yields", "presentation", "requiredExperiment"}, "definition");
    Definition result;
    result.key = text(value, "key", "definition");
    keyValid(result.key);
    const auto& context = result.key;
    if (value.contains("requiredExperiment"))
    {
        if (!value.at("requiredExperiment").is_string()) fail(context, "requiredExperiment must be text");
        result.experiment = value.at("requiredExperiment").get<std::string>();
    }
    const auto& properties = value.at("properties");
#define NAME(field) #field,
    fields(properties, {RESOURCE_FIELDS(NAME) "ecology", "primaryMaterial", "clearConsumption"}, context);
#undef NAME
#define READ(field) property(properties, #field, result.properties.field, context);
    RESOURCE_FIELDS(READ)
#undef READ
    if (properties.contains("ecology"))
        result.properties.ecology = named<ResourceEcology>(text(properties, "ecology", context), EcologyNames, context);
    if (properties.contains("clearConsumption"))
        result.properties.clearConsumption = named<ResourceConsumption>(text(properties, "clearConsumption", context), ConsumptionNames, context);
    if (result.properties.clearConsumption == ResourceConsumption::Infinite)
        fail(context, "clearConsumption must be one or all");
    if (!result.properties.habitatMask || (result.properties.habitatMask & ~15u)) fail(context, "invalid habitatMask");
    if (!result.properties.stockBranchDivisor ||
        result.properties.growthRate > 4 * ResourceRateScale || result.properties.spreadRate > ResourceRateScale)
        fail(context, "invalid growth rate or stock divisor");
    const auto& yields = value.at("yields");
    if (!yields.is_object() || yields.empty() || yields.size() > MaterialCount) fail(context, "yields must name materials");
    for (const auto& entry : yields.items())
    {
        const auto material = parseMaterialKey(entry.key());
        if (!material) fail(context, "unknown material " + entry.key());
        auto& output = result.yields[materialIndex(*material)];
        const auto& input = entry.value();
#define NAME(field) #field,
        fields(input, {YIELD_FIELDS(NAME) "consumption"}, context);
#undef NAME
        if (!input.contains("capacity")) fail(context, "yield requires capacity");
        output.initial = 1;
#define READ(field) property(input, #field, output.field, context);
        YIELD_FIELDS(READ)
#undef READ
        if (input.contains("consumption"))
            output.consumption = named<ResourceConsumption>(text(input, "consumption", context), ConsumptionNames, context);
        if (!output.capacity || output.initial > output.capacity || output.seedReserve > output.capacity || output.growthRate > ResourceRateScale)
            fail(context, "invalid yield capacity, initial stock, reserve or growth rate");
        if (output.placementMaximum && (output.placementMaximum > output.capacity || output.placementMaximum < output.initial))
            fail(context, "placementMaximum must lie between initial stock and capacity");
        if (output.consumption == ResourceConsumption::Infinite && output.destroysDeposit)
            fail(context, "an infinite yield cannot destroy its deposit");
        result.properties.materialMask |= materialBit(*material);
    }
    for (unsigned i = 0; i < MaterialCount; ++i)
        if (result.properties.materialMask & materialBit(static_cast<MaterialId>(i)))
        { result.properties.primaryMaterial = static_cast<MaterialId>(i); break; }
    if (properties.contains("primaryMaterial"))
    {
        const auto material = parseMaterialKey(text(properties, "primaryMaterial", context));
        if (!material || !(result.properties.materialMask & materialBit(*material))) fail(context, "primaryMaterial must be a yield");
        result.properties.primaryMaterial = *material;
    }
    std::uint32_t initialTotal = 0;
    for (const auto& yield : result.yields) initialTotal += yield.initial;
    if (!initialTotal && !result.properties.persistsWhenEmpty) fail(context, "empty initial deposit must persist when empty");
    result.presentation = readPresentation(value.at("presentation"), context);
    return result;
}
Json writeDefinition(const std::string& key, const ResourceProperties& properties, const ResourceYields& yields,
                     const ResourcePresentation& presentation, const std::string& experiment)
{
    Json p = Json::object();
#define WRITE(field) p[#field] = properties.field;
    RESOURCE_FIELDS(WRITE)
#undef WRITE
    p["ecology"] = EcologyNames[static_cast<unsigned>(properties.ecology)];
    p["clearConsumption"] = ConsumptionNames[static_cast<unsigned>(properties.clearConsumption)];
    p["primaryMaterial"] = materialKey(properties.primaryMaterial);
    Json y = Json::object();
    for (unsigned i = 0; i < MaterialCount; ++i)
    {
        if (!yields[i].capacity) continue;
        Json item = Json::object();
#define WRITE(field) item[#field] = yields[i].field;
        YIELD_FIELDS(WRITE)
#undef WRITE
        item["consumption"] = ConsumptionNames[static_cast<unsigned>(yields[i].consumption)];
        y[std::string(materialKey(static_cast<MaterialId>(i)))] = std::move(item);
    }
    Json levels = Json::array();
    for (const auto& level : presentation.levels)
    {
        Json variants = Json::array();
        for (const auto& variant : level.variants) variants.push_back({{"frame", variant.frame}, {"weight", variant.weight}});
        levels.push_back({{"stock", level.stock}, {"variants", std::move(variants)}});
    }
    return {{"key", key}, {"properties", std::move(p)}, {"yields", std::move(y)},
            {"requiredExperiment", experiment},
            {"presentation", {{"name", presentation.name}, {"sprite", presentation.sprite}, {"minimap", presentation.minimap},
                              {"levels", std::move(levels)}, {"animationFrames", presentation.animationFrames},
                              {"animationStride", presentation.animationStride}, {"animationTicks", presentation.animationTicks}}}};
}
std::vector<CatalogExperimentDefinition> readExperiments(const Json& source)
{
    std::vector<CatalogExperimentDefinition> result;
    if (!source.contains("experiments")) return result;
    const auto& entries = source.at("experiments");
    if (!entries.is_array() || entries.size() > ExperimentSet::MAX_STORED) fail("experiments", "invalid declaration array");
    for (const auto& entry : entries)
    {
        fields(entry, {"key", "label", "help"}, "experiment");
        result.push_back({text(entry, "key", "experiment"), text(entry, "label", "experiment", 512), text(entry, "help", "experiment", 4096)});
    }
    validateCatalogExperiments(result);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    return result;
}
std::uint32_t visualHash(std::string_view key)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : key) hash = (hash ^ c) * 16777619u;
    return hash;
}
} // namespace

std::uint16_t ResourcePresentation::frame(std::uint32_t stock, int x, int y, std::uint32_t tick) const
{
    assert(!levels.empty());
    auto level = std::upper_bound(levels.begin(), levels.end(), stock,
                                  [](std::uint32_t value, const auto& entry) { return value < entry.stock; });
    if (level != levels.begin()) --level;
    std::uint32_t hash = visualSeed ^ (std::uint32_t(x) * 0x9e3779b1u) ^ (std::uint32_t(y) * 0x85ebca6bu);
    hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
    std::uint32_t total = 0;
    for (const auto& variant : level->variants) total += variant.weight;
    auto choice = hash % total;
    for (const auto& variant : level->variants)
    {
        if (choice < variant.weight)
            return std::uint16_t(variant.frame + (tick / animationTicks % animationFrames) * animationStride);
        choice -= variant.weight;
    }
    return level->variants.back().frame;
}

std::shared_ptr<const ResourceRegistry> ResourceRegistry::empty()
{
    static const auto registry = [] {
        auto result = std::shared_ptr<ResourceRegistry>(new ResourceRegistry);
        result->compile();
        return std::shared_ptr<const ResourceRegistry>(std::move(result));
    }();
    return registry;
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::availableDefaults()
{
    try { return builtins(); }
    catch (const std::exception&) { return empty(); }
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::legacy()
{
    static constexpr std::string_view definitions =
#include "LegacyResourceDefinitions.inc"
    ;
    static const auto registry = fromJson(definitions);
    return registry;
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::loadDefaultsFile(const std::string& path)
{
    const auto authored = loadFile(path);
    auto json = Json::parse(authored->serialize());
    auto declarations = json["resources"];
    json["resources"] = Json::array();
    // Named stock generator content and the legacy toolbar share these stable
    // slots. Additional resources have no positional meaning and sort by key.
    const auto historical = legacy();
    std::set<std::string> pinned;
    for (unsigned i=0; i<historical->size(); ++i)
    {
        const auto& key = historical->key(static_cast<ResourceId>(i));
        const auto id = authored->find(key);
        if (!id) fail(path, "default catalog is missing historical resource key " + key);
        json["resources"].push_back(declarations[resourceIndex(*id)]);
        pinned.insert(key);
    }
    std::map<std::string, Json> additions;
    for (const auto& definition : declarations)
    {
        const auto key=definition.at("key").get<std::string>();
        if (!pinned.count(key)) additions.emplace(key,definition);
    }
    for (const auto& [key,definition] : additions) json["resources"].push_back(definition);
    return fromJson(json.dump());
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::builtins()
{
    static const auto registry = loadDefaultsFile("data/resources/registry.json");
    return registry;
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::loadFile(const std::string& path)
{
    auto* manager = GAGCore::Toolkit::getFileManager();
    struct Close { void operator()(FILE* f) const { std::fclose(f); } };
    std::unique_ptr<FILE, Close> file(manager ? manager->openFP(path, "rb") : std::fopen(path.c_str(), "rb"));
    if (!file) fail(path, "cannot open file");
    std::string source;
    char bytes[8192];
    for (;;)
    {
        const auto count = std::fread(bytes, 1, sizeof(bytes), file.get());
        source.append(bytes, count);
        if (source.size() > MaximumDefinitionBytes) fail(path, "exceeds 32 MiB");
        if (count < sizeof(bytes))
        {
            if (std::ferror(file.get())) fail(path, "read failed");
            break;
        }
    }
    return fromJson(source);
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::fromJson(std::string_view source)
{
    auto input = parse(source);
    auto registry = std::shared_ptr<ResourceRegistry>(new ResourceRegistry);
    registry->experiments_ = readExperiments(input);
    std::set<std::string> keys;
    for (const auto& entry : input.at("resources"))
    {
        auto definition = readDefinition(entry);
        if (!keys.insert(definition.key).second) fail(definition.key, "duplicate key");
        registry->keys_.push_back(std::move(definition.key));
        registry->properties_.push_back(definition.properties);
        registry->yields_.push_back(definition.yields);
        registry->presentations_.push_back(std::move(definition.presentation));
        registry->requiredExperiments_.push_back(std::move(definition.experiment));
    }
    input = Json();
    registry->compile();
    return registry;
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::deserialize(std::string_view source)
{
    // Embedded resolved snapshots are authoritative; no local file is consulted.
    return fromJson(source);
}
std::shared_ptr<const ResourceRegistry> ResourceRegistry::importJson(std::string_view source) const
{
    auto input = parse(source);
    auto registry = std::shared_ptr<ResourceRegistry>(new ResourceRegistry(*this));
    auto experiments = readExperiments(input);
    for (const auto& experiment : experiments)
    {
        const auto found = std::find_if(registry->experiments_.begin(), registry->experiments_.end(),
                                        [&](const auto& current) { return current.key == experiment.key; });
        if (found == registry->experiments_.end()) registry->experiments_.push_back(experiment);
        else if (*found != experiment) fail(experiment.key, "conflicting experiment declaration");
    }
    validateCatalogExperiments(registry->experiments_);
    std::sort(registry->experiments_.begin(), registry->experiments_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    std::map<std::string, Definition> imports;
    for (const auto& entry : input.at("resources"))
    {
        auto definition = readDefinition(entry);
        const auto key = definition.key;
        if (!imports.emplace(key, std::move(definition)).second) fail(key, "duplicate key");
    }
    for (auto& [key, definition] : imports)
    {
        auto found = registry->find(key);
        if (!found)
        {
            if (registry->size() >= Capacity) fail(key, "definition capacity exceeded");
            const unsigned id = registry->size();
            registry->keys_.push_back(key);
            registry->keyIndex_.emplace(key, static_cast<ResourceId>(id));
            registry->properties_.push_back({});
            registry->yields_.push_back({});
            registry->presentations_.push_back({});
            registry->requiredExperiments_.push_back({});
            found = static_cast<ResourceId>(id);
        }
        const auto id = resourceIndex(*found);
        registry->properties_[id] = definition.properties;
        registry->yields_[id] = definition.yields;
        registry->presentations_[id] = std::move(definition.presentation);
        registry->requiredExperiments_[id] = std::move(definition.experiment);
    }
    input = Json();
    imports.clear();
    registry->compile();
    return registry;
}
std::string ResourceRegistry::serialize() const
{
    Json definitions = Json::array(), experiments = Json::array();
    for (unsigned id = 0; id < size(); ++id)
        definitions.push_back(writeDefinition(keys_[id], properties_[id], yields_[id], presentations_[id], requiredExperiments_[id]));
    for (const auto& experiment : experiments_)
        experiments.push_back({{"key", experiment.key}, {"label", experiment.label}, {"help", experiment.help}});
    auto result = Json{{"schemaVersion", 1}, {"resources", std::move(definitions)}, {"experiments", std::move(experiments)}}.dump();
    if (result.size() > MaximumDefinitionBytes) fail("snapshot", "resolved definitions exceed 32 MiB");
    return result;
}
std::optional<ResourceId> ResourceRegistry::find(std::string_view key) const
{
    const auto found = keyIndex_.find(key);
    if (found == keyIndex_.end()) return std::nullopt;
    return found->second;
}
std::vector<std::string> ResourceRegistry::experimentKeys() const
{
    std::vector<std::string> result;
    for (const auto& experiment : experiments_) result.push_back(experiment.key);
    return result;
}
void ResourceRegistry::compile()
{
    mutableMaterialSources_ = 0;
    for (unsigned id = 0; id < size(); ++id)
    {
        const auto& p = properties_[id];
        const auto& yields = yields_[id];
        // Harvesting any destructive secondary yield also removes an infinite
        // primary source. Do not infer mutability from just the selected yield.
        bool destroysSource = p.clearable;
        for (const auto& yield : yields)
            if (yield.capacity && (yield.consumption == ResourceConsumption::All || yield.destroysDeposit))
                destroysSource = true;
        for (unsigned material = 0; material < MaterialCount; ++material)
        {
            const auto& yield = yields[material];
            if (!yield.capacity) continue;
            const bool renews = p.growthRate && p.ecology != ResourceEcology::None
                && (yield.growthRate || p.spreadRate);
            if (destroysSource || yield.consumption != ResourceConsumption::Infinite || renews)
                mutableMaterialSources_ |= MaterialMask(1u << material);
        }
    }
    keyIndex_.clear();
    for (unsigned id = 0; id < size(); ++id) keyIndex_.emplace(keys_[id], static_cast<ResourceId>(id));
    const auto allowed = experimentKeys();
    for (unsigned id = 0; id < size(); ++id)
    {
        const auto& experiment = requiredExperiments_[id];
        if (!experiment.empty() && !parseExperimentKey(experiment) &&
            std::find(allowed.begin(), allowed.end(), experiment) == allowed.end())
            fail(keys_[id], "undeclared requiredExperiment " + experiment);
        presentations_[id].visualSeed = visualHash(keys_[id]);
    }
    const auto canonical = serialize();
    const auto hash = Online::Sha256::digest(canonical);
    digest_ = Online::Sha256::toHex(hash);
    checksum_ = std::uint32_t(hash[0]) | std::uint32_t(hash[1]) << 8 | std::uint32_t(hash[2]) << 16 | std::uint32_t(hash[3]) << 24;
}
