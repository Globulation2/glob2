// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingType.h"
#include "ExperimentalFeatures.h"
#include "Sha256.h"
#include "UnitUtils.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <type_traits>
#include <tuple>

namespace
{
using Json = nlohmann::json;
constexpr std::size_t MAX_CATALOG_BYTES = 8 * 1024 * 1024;
constexpr std::size_t MAX_CATALOG_VARIANTS = 4096;
constexpr const char* ABILITY_NAMES[NB_ABILITY] = {
    "stopWalk", "stopSwim", "stopFly", "walk", "swim", "fly", "build", "harvest",
    "attackSpeed", "attackStrength", "magicAttackAir", "magicAttackGround", "magicCreateWood", "magicCreateWheat", "magicCreateAlgae", "armor", "hp"};
constexpr const char* UNIT_NAMES[NB_UNIT_TYPE] = {"worker", "explorer", "warrior"};
constexpr const char* RESOURCE_NAMES[MAX_RESOURCES] = {
    "wood", "wheat", "papyrus", "stone", "algae", "cherry", "orange", "prune"};

#define BUILDING_STRING_FIELDS(X) X(type) X(gameSprite) X(miniSprite)
#define BUILDING_INT_FIELDS(X) \
    X(gameSpriteImage) X(gameSpriteCount) X(miniSpriteImage) X(hueImage) X(flagImage) \
    X(crossConnectMultiImage) X(foodable) X(fillable) \
    X(zonableForbidden) \
    X(insideSpeed) X(width) X(height) X(decLeft) X(decTop) \
    X(isCloaked) X(shootingRange) X(shootDamage) X(shootSpeed) X(shootRhythm) \
    X(maxBullets) X(multiplierStoneToBullets) \
    X(maxUnitInside) X(hpInit) X(hpMax) X(hpInc) X(armor) X(level) \
    X(shortTypeNum) X(isBuildingSite) X(defaultUnitStayRange) X(maxUnitStayRange) \
    X(viewingRange) X(regenerationSpeed) X(prestige)
#define BUILDING_ARRAY_FIELDS(X) X(zonable) X(maxResource) X(multiplierResource)

[[noreturn]] void fail(const std::string& context, const std::string& message)
{
    throw std::runtime_error("Building catalog " + context + ": " + message);
}
void object(const Json& value, const std::string& context)
{
    if (!value.is_object()) fail(context, "expected an object");
}
void keys(const Json& j, std::initializer_list<const char*> allowed, const std::string& context)
{
    object(j, context);
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            fail(context, "unknown field '" + it.key() + "'");
}
Sint32 integer(const Json& j, const std::string& context)
{
    if (!j.is_number_integer()) fail(context, "expected an integer");
    if (j.is_number_unsigned())
    {
        const auto value = j.get<std::uint64_t>();
        if (value > std::uint64_t(std::numeric_limits<Sint32>::max())) fail(context, "integer out of range");
        return static_cast<Sint32>(value);
    }
    const auto value = j.get<std::int64_t>();
    if (value < std::numeric_limits<Sint32>::min() || value > std::numeric_limits<Sint32>::max())
        fail(context, "integer out of range");
    return static_cast<Sint32>(value);
}
std::string string(const Json& j, const std::string& context)
{
    if (!j.is_string()) fail(context, "expected a string");
    auto s = j.get<std::string>();
    if (s.size() > 1024 || s.find('\0') != std::string::npos) fail(context, "invalid string length or NUL byte");
    return s;
}
void stableKey(const std::string& key, const std::string& context)
{
    if (key.empty() || key.size() > 128) fail(context, "stable key must contain 1..128 characters");
    for (unsigned char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            fail(context, "invalid stable key '" + key + "'");
}
void range(Sint32 v, Sint32 lo, Sint32 hi, const std::string& context)
{
    if (v < lo || v > hi) fail(context, "value outside " + std::to_string(lo) + ".." + std::to_string(hi));
}
template<class T> void optional(const Json& j, const char* key, T& target)
{
    auto i = j.find(key);
    if (i == j.end()) return;
    if constexpr (std::is_same_v<T, bool>)
    {
        if (!i->is_boolean()) fail(key, "expected a boolean");
        target = i->get<bool>();
    }
    else if constexpr (std::is_same_v<T, std::string>) target = string(*i, key);
    else
    {
        const Sint32 value = integer(*i, key);
        if constexpr (std::is_unsigned_v<T>) if (value < 0) fail(key, "expected nonnegative value");
        target = static_cast<T>(value);
    }
}
template<class A> void array(const Json& j, A& target, const std::string& context)
{
    if (!j.is_array() || j.size() != std::size(target)) fail(context, "incorrect array length");
    for (std::size_t i = 0; i < j.size(); ++i) target[i] = integer(j[i], context);
}
BuildingResourceCost cost(const Json& j)
{
    BuildingResourceCost out{};
    object(j, "cost");
    for (auto i = j.begin(); i != j.end(); ++i)
    {
        int resource = -1;
        for (int r = 0; r < MAX_RESOURCES; ++r) if (i.key() == RESOURCE_NAMES[r]) resource = r;
        if (resource < 0) fail("cost", "unknown resource '" + i.key() + "'");
        out[resource] = integer(i.value(), i.key());
        range(out[resource], 0, 1000000, i.key());
    }
    return out;
}
Json costJson(const BuildingResourceCost& values)
{
    Json j = Json::object();
    for (int r = 0; r < MAX_RESOURCES; ++r) if (values[r]) j[RESOURCE_NAMES[r]] = values[r];
    return j;
}
BuildingServiceSpec service(const Json& j)
{
    keys(j, {"enabled", "unitMask", "duration", "cost", "partial", "holdAdmissionUntilExit", "optionalFruitMask", "convertsUnits"}, "service");
    BuildingServiceSpec s;
    optional(j, "enabled", s.enabled); optional(j, "unitMask", s.unitMask);
    optional(j, "duration", s.duration); optional(j, "holdAdmissionUntilExit", s.holdAdmissionUntilExit);
    optional(j, "optionalFruitMask", s.optionalFruitMask); optional(j, "convertsUnits", s.convertsUnits);
    if (j.contains("cost")) s.cost = cost(j.at("cost"));
    const std::string p = j.value("partial", "none");
    if (p == "proportional_full_cost") s.partial = BuildingPartialService::ProportionalFullCost;
    else if (p != "none") fail("service.partial", "unknown policy");
    return s;
}
Json serviceJson(const BuildingServiceSpec& s)
{
    return {{"enabled", s.enabled}, {"unitMask", s.unitMask}, {"duration", s.duration},
        {"cost", costJson(s.cost)}, {"partial", s.partial == BuildingPartialService::None ? "none" : "proportional_full_cost"},
        {"holdAdmissionUntilExit", s.holdAdmissionUntilExit}, {"optionalFruitMask", s.optionalFruitMask}, {"convertsUnits", s.convertsUnits}};
}
std::uint8_t resourceMask(const Json& j, const std::string& context)
{
    if (!j.is_array()) fail(context, "expected an array of resource names");
    std::uint8_t mask=0;
    for (const auto& item : j)
    {
        const auto name=string(item,context);
        int resource=-1;
        for (int r=0; r<MAX_RESOURCES; ++r) if (name==RESOURCE_NAMES[r]) resource=r;
        if (resource<0) fail(context,"unknown resource '"+name+"'");
        if (mask&(1u<<resource)) fail(context,"duplicate resource '"+name+"'");
        mask|=1u<<resource;
    }
    return mask;
}
Json resourceMaskJson(std::uint8_t mask)
{
    auto j=Json::array();
    for (int r=0; r<MAX_RESOURCES; ++r) if (mask&(1u<<r)) j.push_back(RESOURCE_NAMES[r]);
    return j;
}
BuildingSemantics semantics(const Json& j)
{
    keys(j, {"replenishResources", "requiredWorkerLevel", "assignmentLimit", "regenerationPerTick", "repairable", "constructionCost", "repairCost", "placeable", "instantPlacement", "relocatable", "occupiesGround",
        "admittedUnitMask", "workPriorityBias", "sightSharing", "feeding", "healing", "training", "trainingInParallel",
        "production", "market", "projectileDamage", "projectileBuildingDamage", "ammunitionResource", "ammunitionCost"}, "semantics");
    BuildingSemantics s;
    if (j.contains("replenishResources")) s.replenishResourceMask=resourceMask(j.at("replenishResources"),"replenishResources");
#define READ(n) optional(j, #n, s.n)
    READ(requiredWorkerLevel); READ(assignmentLimit); READ(regenerationPerTick); READ(repairable); READ(placeable); READ(instantPlacement); READ(relocatable);
    READ(occupiesGround); READ(admittedUnitMask); READ(workPriorityBias); READ(trainingInParallel);
    READ(projectileBuildingDamage); READ(ammunitionResource); READ(ammunitionCost);
#undef READ
    if (j.contains("constructionCost")) s.constructionCost = cost(j.at("constructionCost"));
    if (j.contains("repairCost")) s.repairCost = cost(j.at("repairCost"));
    const auto sight = j.value("sightSharing", "other");
    if (sight == "food") s.sightSharing = BuildingSightSharing::Food;
    else if (sight == "exchange") s.sightSharing = BuildingSightSharing::Exchange;
    else if (sight != "other") fail("sightSharing", "unknown category");
    if (j.contains("feeding")) s.feeding = service(j.at("feeding"));
    if (j.contains("healing")) s.healing = service(j.at("healing"));
    if (j.contains("projectileDamage")) array(j.at("projectileDamage"), s.projectileDamage, "projectileDamage");
    if (j.contains("training"))
    {
        const auto& list = j.at("training");
        if (!list.is_object()) fail("training", "expected ability-name object");
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            int a = -1;
            for (int i = 0; i < NB_ABILITY; ++i) if (it.key() == ABILITY_NAMES[i]) a = i;
            if (a < 0) fail("training", "unknown ability '" + it.key() + "'");
            const auto& t = it.value(); auto& out = s.training[a];
            keys(t, {"enabled", "unitMask", "targetLevel", "duration", "cost", "constructionLevel"}, "training");
            optional(t, "enabled", out.enabled); optional(t, "unitMask", out.unitMask);
            optional(t, "targetLevel", out.targetLevel); optional(t, "duration", out.duration);
            optional(t, "constructionLevel", out.constructionLevel);
            if (t.contains("cost")) out.cost = cost(t.at("cost"));
        }
    }
    if (j.contains("production"))
    {
        const auto& p = j.at("production");
        keys(p, {"scheduling", "recipes", "fallbackUnit", "initialRatios"}, "production");
        auto policy = p.value("scheduling", "weighted_committed_job");
        if (policy == "weighted_late_choice") s.production.scheduling = BuildingProductionScheduling::WeightedLateChoice;
        else if (policy != "weighted_committed_job") fail("production.scheduling", "unknown policy");
        optional(p, "fallbackUnit", s.production.fallbackUnit);
        if (p.contains("initialRatios")) array(p.at("initialRatios"), s.production.initialRatios, "initialRatios");
        if (p.contains("recipes"))
        {
            const auto& recipes = p.at("recipes");
            if (!recipes.is_object()) fail("recipes", "expected unit-name object");
            for (auto it = recipes.begin(); it != recipes.end(); ++it)
            {
                int u = -1;
                for (int i = 0; i < NB_UNIT_TYPE; ++i) if (it.key() == UNIT_NAMES[i]) u = i;
                if (u < 0) fail("recipes", "unknown unit '" + it.key() + "'");
                const auto& r = it.value(); auto& out = s.production.recipes[u];
                keys(r, {"enabled", "duration", "cost"}, "recipe");
                optional(r, "enabled", out.enabled); optional(r, "duration", out.duration);
                if (r.contains("cost")) out.cost = cost(r.at("cost"));
            }
        }
    }
    if (j.contains("market"))
    {
        const auto& m = j.at("market");
        keys(m, {"sharedStock", "interTeamFruitExchange", "suppliesStock", "suppliesStockExperiment",
            "fetchesStock", "fetchesStockExperiment", "pickupPenalty", "suppliesDirectStock", "fetchesDirectStock", "suppliesStockResources", "suppliesDirectStockResources", "fetchesStockResources", "fetchesDirectStockResources"}, "market");
#define READ(n) optional(m, #n, s.market.n)
        READ(sharedStock); READ(interTeamFruitExchange); READ(suppliesStock); READ(suppliesStockExperiment);
        READ(fetchesStock); READ(fetchesStockExperiment); READ(pickupPenalty); READ(suppliesDirectStock); READ(fetchesDirectStock);
        for (auto [name, mask] : {std::pair{"suppliesStockResources", &s.market.suppliesStockMask},
            {"suppliesDirectStockResources", &s.market.suppliesDirectStockMask},
            {"fetchesStockResources", &s.market.fetchesStockMask}, {"fetchesDirectStockResources", &s.market.fetchesDirectStockMask}})
            if (m.contains(name)) *mask=resourceMask(m.at(name),name);
#undef READ
    }
    return s;
}
Json semanticsJson(const BuildingSemantics& s)
{
    Json j;
    j["replenishResources"]=resourceMaskJson(s.replenishResourceMask);
#define WRITE(n) j[#n] = s.n
    WRITE(requiredWorkerLevel); WRITE(assignmentLimit); WRITE(regenerationPerTick); WRITE(repairable); WRITE(placeable); WRITE(instantPlacement); WRITE(relocatable);
    WRITE(occupiesGround); WRITE(admittedUnitMask); WRITE(workPriorityBias); WRITE(trainingInParallel);
    WRITE(projectileDamage); WRITE(projectileBuildingDamage); WRITE(ammunitionResource); WRITE(ammunitionCost);
#undef WRITE
    j["constructionCost"] = costJson(s.constructionCost);
    j["repairCost"] = costJson(s.repairCost);
    j["sightSharing"] = s.sightSharing == BuildingSightSharing::Food ? "food" : s.sightSharing == BuildingSightSharing::Exchange ? "exchange" : "other";
    j["feeding"] = serviceJson(s.feeding); j["healing"] = serviceJson(s.healing);
    auto& training = j["training"] = Json::object();
    for (int a = 0; a < NB_ABILITY; ++a)
    {
        const auto& t = s.training[a];
        if (t.enabled || t.unitMask != BUILDING_ALL_UNIT_TYPES || t.targetLevel || t.duration || t.constructionLevel >= 0 || t.cost != BuildingResourceCost{})
            training[ABILITY_NAMES[a]] = {{"enabled", t.enabled}, {"unitMask", t.unitMask},
                {"targetLevel", t.targetLevel}, {"duration", t.duration}, {"cost", costJson(t.cost)}};
        if (t.constructionLevel >= 0) training[ABILITY_NAMES[a]]["constructionLevel"] = t.constructionLevel;
    }
    auto& p = j["production"];
    p["scheduling"] = s.production.scheduling == BuildingProductionScheduling::WeightedLateChoice ? "weighted_late_choice" : "weighted_committed_job";
    p["fallbackUnit"] = s.production.fallbackUnit; p["initialRatios"] = s.production.initialRatios;
    p["recipes"] = Json::object();
    for (int u = 0; u < NB_UNIT_TYPE; ++u)
    {
        const auto& r = s.production.recipes[u];
        if (r.enabled || r.duration || r.cost != BuildingResourceCost{})
            p["recipes"][UNIT_NAMES[u]] = {{"enabled", r.enabled}, {"duration", r.duration}, {"cost", costJson(r.cost)}};
    }
    auto& m = j["market"];
    m["suppliesStockResources"]=resourceMaskJson(s.market.suppliesStockMask);
    m["suppliesDirectStockResources"]=resourceMaskJson(s.market.suppliesDirectStockMask);
    m["fetchesStockResources"]=resourceMaskJson(s.market.fetchesStockMask);
    m["fetchesDirectStockResources"]=resourceMaskJson(s.market.fetchesDirectStockMask);
#define WRITE(n) m[#n] = s.market.n
    WRITE(sharedStock); WRITE(interTeamFruitExchange); WRITE(suppliesStock); WRITE(suppliesStockExperiment);
    WRITE(fetchesStock); WRITE(fetchesStockExperiment); WRITE(pickupPenalty); WRITE(suppliesDirectStock); WRITE(fetchesDirectStock);
#undef WRITE
    return j;
}
Json presentationJson(const BuildingPresentationSpec& p)
{
    return {{"displayName", p.displayName}, {"iconFrame", p.iconFrame}, {"iconTile", p.iconTile},
        {"iconPriority", p.iconPriority}, {"skinSlot", p.skinSlot}, {"showLevel", p.showLevel},
        {"connectionGroup", p.connectionGroup}, {"connectsAcrossTeams", p.connectsAcrossTeams}, {"defaultAssigned",p.defaultAssigned}};
}
void presentation(const Json& j, BuildingPresentationSpec& p)
{
    keys(j, {"displayName", "iconFrame", "iconTile", "iconPriority", "skinSlot", "showLevel", "connectionGroup", "connectsAcrossTeams", "defaultAssigned"}, "presentation");
#define READ(n) optional(j, #n, p.n)
    READ(displayName); READ(iconFrame); READ(iconTile); READ(iconPriority); READ(skinSlot);
    READ(showLevel); READ(connectionGroup); READ(connectsAcrossTeams); READ(defaultAssigned);
#undef READ
}

Json propertiesJson(const BuildingType& b)
{
    Json j;
#define WRITE(n) j[#n] = b.n;
    BUILDING_STRING_FIELDS(WRITE) BUILDING_INT_FIELDS(WRITE) BUILDING_ARRAY_FIELDS(WRITE)
#undef WRITE
    return j;
}
void properties(const Json& j, BuildingType& b)
{
#define NAME(n) #n,
    keys(j, { BUILDING_STRING_FIELDS(NAME) BUILDING_INT_FIELDS(NAME) BUILDING_ARRAY_FIELDS(NAME) }, "properties");
#undef NAME
#define READ(n) optional(j, #n, b.n);
    BUILDING_STRING_FIELDS(READ) BUILDING_INT_FIELDS(READ)
#undef READ
#define READ(n) if (j.contains(#n)) array(j.at(#n), b.n, #n);
    BUILDING_ARRAY_FIELDS(READ)
#undef READ
    if (!j.contains("shortTypeNum")) b.shortTypeNum = -1;
    if (!j.contains("type")) b.type.clear();
}
std::string readFile(const std::string& path)
{
    auto* fm = GAGCore::Toolkit::getFileManager();
    struct CloseFile { void operator()(FILE* file) const { std::fclose(file); } };
    std::unique_ptr<FILE, CloseFile> file(fm ? fm->openFP(path, "rb") : std::fopen(path.c_str(), "rb"));
    if (!file) fail(path, "cannot open file");
    std::string data;
    char chunk[8192];
    for (;;)
    {
        const auto count = std::fread(chunk, 1, sizeof(chunk), file.get());
        data.append(chunk, count);
        if (data.size() > MAX_CATALOG_BYTES) fail(path, "file is too large");
        if (count < sizeof(chunk))
        {
            if (std::ferror(file.get())) fail(path, "read failed");
            break;
        }
    }
    return data;
}
Json parse(const std::string& text)
{
    if (text.size() > MAX_CATALOG_BYTES) fail("JSON", "snapshot is too large");
    // Reject duplicate keys rather than silently accepting a misspelled override.
    std::vector<std::unordered_set<std::string>> objects;
    return Json::parse(text, [&objects](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 64) fail("JSON", "nesting limit exceeded");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        else if (event == Json::parse_event_t::key)
        {
            if (!objects.back().insert(value.get<std::string>()).second) fail("JSON", "duplicate key");
        }
        else if (event == Json::parse_event_t::object_end) objects.pop_back();
        return true;
    });
}
}

void BuildingsTypes::loadManifest(const std::string& path)
{
    Json manifest = parse(readFile(path));
    keys(manifest, {"schemaVersion", "catalogKey", "startingBuilding", "experiments", "files"}, "manifest");
    Json snapshot = manifest;
    snapshot.erase("files"); snapshot["variants"] = Json::array();
    if (!manifest.contains("files") || !manifest.at("files").is_array() || manifest.at("files").size() > MAX_CATALOG_VARIANTS) fail(path, "missing files array");
    const auto slash = path.find_last_of("/\\");
    const std::string directory = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    std::set<std::string> seen;
    for (const auto& f : manifest.at("files"))
    {
        const std::string filename = string(f, "filename");
        // Definition files are local siblings; snapshots never perform file I/O.
        if (filename.empty() || filename.find_first_of("/\\:") != std::string::npos || filename == "." || filename == "..")
            fail(path, "definition filename must be a basename");
        if (!seen.insert(filename).second) fail(path, "duplicate definition filename");
        const Json definitions = parse(readFile(directory + filename));
        keys(definitions, {"variants"}, filename);
        if (!definitions.contains("variants") || !definitions.at("variants").is_array()) fail(filename, "missing variants array");
        for (auto v : definitions.at("variants"))
        {
            // Authored files identify variants by stable key. Dense IDs belong
            // to this compiled snapshot, so deleting a file never requires renumbering others.
            v["id"] = snapshot["variants"].size();
            snapshot["variants"].push_back(std::move(v));
        }
        if (snapshot["variants"].size() > MAX_CATALOG_VARIANTS) fail(path, "too many variants");
    }
    loadSnapshotJson(snapshot.dump());
}

void BuildingsTypes::loadSnapshotJson(const std::string& text)
{
    const Json root = parse(text);
    keys(root, {"schemaVersion", "catalogKey", "startingBuilding", "experiments", "variants"}, "snapshot");
    if (!root.contains("schemaVersion") || integer(root.at("schemaVersion"), "schemaVersion") != 1)
        fail("snapshot", "unsupported schemaVersion");
    BuildingsTypes parsed;
    parsed.catalogKey_ = string(root.at("catalogKey"), "catalogKey");
    optional(root, "startingBuilding", parsed.startingBuildingKey_);
    stableKey(parsed.catalogKey_, "catalogKey");
    if (root.contains("experiments"))
    {
        if (!root.at("experiments").is_array() || root.at("experiments").size() > ExperimentSet::MAX_STORED) fail("experiments", "invalid metadata array");
        for (const auto& e : root.at("experiments"))
        {
            keys(e, {"key", "label", "help"}, "experiment");
            parsed.experiments_.push_back({string(e.at("key"), "experiment.key"),
                string(e.at("label"), "experiment.label"), string(e.at("help"), "experiment.help")});
        }
    }
    const auto& variants = root.at("variants");
    if (!variants.is_array() || variants.empty() || variants.size() > MAX_CATALOG_VARIANTS) fail("variants", "invalid variant count");
    parsed.entries_->resize(variants.size());
    std::vector<bool> seen(variants.size());
    for (const auto& v : variants)
    {
        keys(v, {"id", "key", "previous", "next", "requiredExperiment", "properties", "semantics", "presentation"}, "variant");
        const Sint32 id = integer(v.at("id"), "id");
        if (id < 0 || std::size_t(id) >= variants.size() || seen[id]) fail("id", "IDs must be unique and dense from zero");
        seen[id] = true;
        auto& b = (*parsed.entries_)[id];
        b.key = string(v.at("key"), "key");
        optional(v, "previous", b.previousKey); optional(v, "next", b.nextKey);
        optional(v, "requiredExperiment", b.requiredExperiment);
        properties(v.at("properties"), b);
        b.semantics = semantics(v.at("semantics"));
        if (v.contains("presentation")) presentation(v.at("presentation"), b.presentation);
    }
    parsed.resolveAndValidate();
    if (parsed.snapshotJson().size() > MAX_CATALOG_BYTES)
        fail("JSON", "resolved catalog exceeds the 8 MiB snapshot limit");
    *this = std::move(parsed); // Atomic: invalid input cannot damage the active catalog.
}

std::string BuildingsTypes::snapshotJson() const
{
    Json j = {{"schemaVersion", 1}, {"catalogKey", catalogKey_}, {"startingBuilding", startingBuildingKey_}, {"experiments", Json::array()}, {"variants", Json::array()}};
    for (const auto& e : experiments_) j["experiments"].push_back({{"key", e.key}, {"label", e.label}, {"help", e.help}});
    for (std::size_t id = 0; id < entries_->size(); ++id)
    {
        const auto& b = (*entries_)[id];
        j["variants"].push_back({{"id", id}, {"key", b.key}, {"previous", b.previousKey}, {"next", b.nextKey},
            {"requiredExperiment", b.requiredExperiment}, {"properties", propertiesJson(b)}, {"semantics", semanticsJson(b.semantics)}, {"presentation", presentationJson(b.presentation)}});
    }
    return j.dump();
}

std::string BuildingsTypes::fingerprint() const
{
    return Online::Sha256::hex(snapshotJson());
}

void BuildingsTypes::resolveAndValidate()
{
    std::set<std::string> variantKeys, experimentKeys;
    std::map<std::string, Sint32> connectionGroups;
    std::set<std::tuple<std::string, int, bool>> oldNames;
    std::vector<CatalogExperimentDefinition> definitions;
    for (const auto& e : experiments_) definitions.push_back({e.key,e.label,e.help});
    validateCatalogExperiments(definitions);
    std::sort(experiments_.begin(), experiments_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    for (const auto& e : experiments_)
    {
        stableKey(e.key, "experiment.key");
        if (!experimentKeys.insert(e.key).second) fail(e.key, "duplicate experiment");
    }
    const auto gate = [&](const std::string& key) {
        if (!key.empty() && !experimentKeys.count(key)) fail(key, "experiment metadata is missing");
    };
    for (const auto& b : *entries_)
    {
        stableKey(b.key, "variant.key");
        if (!variantKeys.insert(b.key).second) fail(b.key, "duplicate stable key");
        if (!b.type.empty() && !oldNames.emplace(b.type, b.level, b.isBuildingSite != 0).second) fail(b.key, "duplicate family/level/site variant");
    }
    for (auto& b : *entries_)
    {
        if (b.presentation.displayName.empty()) b.presentation.displayName = b.type.empty() ? b.key : b.type;
        if (b.crossConnectMultiImage && b.presentation.connectionGroup.empty()) b.presentation.connectionGroup = b.key;
        if (!b.presentation.connectionGroup.empty())
        {
            stableKey(b.presentation.connectionGroup, "presentation.connectionGroup");
            b.presentation.connectionGroupId = connectionGroups.emplace(b.presentation.connectionGroup, connectionGroups.size()).first->second;
        }
        range(b.presentation.iconFrame, 0, 1000000, "presentation.iconFrame");
        range(b.presentation.iconPriority, 0, 1000000, "presentation.iconPriority");
        gate(b.requiredExperiment); gate(b.semantics.market.suppliesStockExperiment); gate(b.semantics.market.fetchesStockExperiment);
        // During migration the legacy execution fields are projections, not an
        // independent source of behavior. Authors change semantic descriptors.
        const auto& semantic = b.semantics;
        b.canFeedUnit = semantic.feeding.enabled; b.timeToFeedUnit = semantic.feeding.duration;
        b.canHealUnit = semantic.healing.enabled; b.timeToHealUnit = semantic.healing.duration;
        b.upgradeInParallel = semantic.trainingInParallel;
        b.useTeamResources = semantic.market.sharedStock;
        b.isVirtual = !semantic.occupiesGround;
        b.maxUnitWorking = semantic.assignmentLimit > 0;
        if (b.presentation.defaultAssigned < 0) b.presentation.defaultAssigned = std::min(2,semantic.assignmentLimit);
        range(b.presentation.defaultAssigned, 0, semantic.assignmentLimit, b.key + ".presentation.defaultAssigned");
        b.canExchange = semantic.market.interTeamFruitExchange;
        for (int a = 0; a < NB_ABILITY; ++a)
        {
            b.upgrade[a] = semantic.training[a].enabled;
            b.upgradeTime[a] = semantic.training[a].duration;
        }
        b.semantics.production.enabledUnitMask = 0;
        b.unitProductionTime = b.resourceForOneUnit = 0;
        for (int u = NB_UNIT_TYPE - 1; u >= 0; --u)
            if (semantic.production.recipes[u].enabled)
            {
                b.semantics.production.enabledUnitMask |= 1u << u;
                b.unitProductionTime = semantic.production.recipes[u].duration;
                b.resourceForOneUnit = semantic.production.recipes[u].cost[WHEAT];
            }

        b.prevLevel = b.previousKey.empty() ? -1 : findByKey(b.previousKey);
        b.nextLevel = b.nextKey.empty() ? -1 : findByKey(b.nextKey);
        if ((!b.previousKey.empty() && b.prevLevel < 0) || (!b.nextKey.empty() && b.nextLevel < 0)) fail(b.key, "unresolved transition");
        for (const auto value : {b.hueImage, b.crossConnectMultiImage, b.upgradeInParallel, b.foodable, b.fillable,
            b.zonableForbidden, b.canFeedUnit, b.canHealUnit, b.canExchange, b.useTeamResources, b.isVirtual, b.isCloaked, b.isBuildingSite})
            range(value, 0, 1, b.key + ".boolean property");
        for (const auto value : {b.gameSpriteImage, b.gameSpriteCount, b.flagImage, b.timeToFeedUnit, b.timeToHealUnit,
            b.shootDamage, b.shootSpeed, b.shootRhythm, b.maxBullets, b.multiplierStoneToBullets,
            b.unitProductionTime, b.resourceForOneUnit, b.viewingRange, b.regenerationSpeed, b.prestige})
            range(value, 0, 1000000, b.key + ".nonnegative property");
        range(b.gameSpriteCount, 1, 65535, b.key + ".gameSpriteCount");
        range(b.miniSpriteImage, -1, 1000000, b.key + ".miniSpriteImage");
        range(b.decLeft, -64, 64, b.key + ".decLeft"); range(b.decTop, -64, 64, b.key + ".decTop");
        for (int a = 0; a < NB_ABILITY; ++a)
        {
            range(b.upgrade[a], 0, 1, b.key + ".upgrade");
            range(b.upgradeTime[a], 0, 1000000, b.key + ".upgradeTime");
        }
        for (const auto value : b.zonable) range(value, 0, 1, b.key + ".zonable");
        range(b.width, 1, 64, b.key + ".width"); range(b.height, 1, 64, b.key + ".height");
        range(b.level, 0, 3, b.key + ".level"); range(b.shortTypeNum, -1, 4095, b.key + ".shortTypeNum");
        range(b.hpInit, 0, 1000000, b.key + ".hpInit"); range(b.hpMax, 0, 1000000, b.key + ".hpMax");
        if (b.hpInit > b.hpMax) fail(b.key, "initial health exceeds maximum health");
        if (b.semantics.repairable && b.hpMax == 0) fail(b.key, "repairable buildings require positive maximum health");
        range(b.hpInc, 0, 1000000, b.key + ".hpInc"); range(b.armor, 0, 1000000, b.key + ".armor");
        range(b.maxUnitInside, 0, 32767, b.key + ".maxUnitInside"); range(b.maxUnitWorking, 0, 32767, b.key + ".maxUnitWorking");
        range(b.defaultUnitStayRange, 0, 1024, b.key + ".defaultUnitStayRange");
        range(b.maxUnitStayRange, b.defaultUnitStayRange, 1024, b.key + ".maxUnitStayRange");
        range(b.insideSpeed, 1, 256, b.key + ".insideSpeed");
        for (int r = 0; r < MAX_NB_RESOURCES; ++r)
        {
            range(b.maxResource[r], 0, r < MAX_RESOURCES ? 1000000 : 0, b.key + ".maxResource");
            range(b.multiplierResource[r], 1, 1000000, b.key + ".multiplierResource");
        }
        auto& s = b.semantics;
        const auto compileCost = [&](auto& recipe) {
            recipe.costMask = 0;
            for (int r = 0; r < MAX_NB_RESOURCES; ++r)
            {
                range(recipe.cost[r], 0, r < MAX_RESOURCES ? 1000000 : 0, b.key + ".cost");
                if (recipe.cost[r]) recipe.costMask |= std::uint16_t(1u << r);
            }
        };
        for (int r=0; r<MAX_NB_RESOURCES; ++r)
        {
            range(s.constructionCost[r], 0, r<MAX_RESOURCES ? 1000000 : 0, b.key + ".constructionCost");
            range(s.repairCost[r], 0, r<MAX_RESOURCES ? 1000000 : 0, b.key + ".repairCost");
            if (!b.isBuildingSite && s.constructionCost[r]) fail(b.key, "constructionCost belongs to a construction site");
        }
        compileCost(s.feeding); compileCost(s.healing);
        for (int ability=0; ability<WALK; ++ability)
            if (s.training[ability].enabled) fail(b.key, "idle movement primitives cannot be trained; configure walk, swim, or fly instead");
        s.trainingCostMask = 0;
        for (auto& recipe : s.training)
        {
            compileCost(recipe);
            if (recipe.enabled) s.trainingCostMask |= recipe.costMask;
        }
        for (auto& recipe : s.production.recipes) compileCost(recipe);
        range(s.requiredWorkerLevel, 0, 3, b.key + ".requiredWorkerLevel");
        range(s.assignmentLimit, 0, UnitUtils::MAX_COUNT, b.key + ".assignmentLimit");
        range(s.regenerationPerTick, 0, 1000000, b.key + ".regenerationPerTick");
        range(s.admittedUnitMask, 0, BUILDING_ALL_UNIT_TYPES, b.key + ".admittedUnitMask");
        range(s.workPriorityBias, 0, 1000000, b.key + ".workPriorityBias");
        for (const auto* service : {&s.feeding, &s.healing})
        {
            range(service->unitMask, 0, BUILDING_ALL_UNIT_TYPES, b.key + ".service.unitMask");
            range(service->duration, 0, 1000000, b.key + ".service.duration");
            range(service->optionalFruitMask, 0, (1 << HAPPINESS_COUNT) - 1, b.key + ".optionalFruitMask");
            if (service->enabled && (!(service->unitMask & s.admittedUnitMask) || b.maxUnitInside == 0)) fail(b.key, "service has no admitted occupants");
        }
        for (const auto& t : s.training)
        {
            range(t.unitMask, 0, BUILDING_ALL_UNIT_TYPES, b.key + ".training.unitMask");
            range(t.duration, 0, 1000000, b.key + ".training.duration");
            range(t.targetLevel, 0, 3, b.key + ".training.targetLevel");
            range(t.constructionLevel, -1, 3, b.key + ".training.constructionLevel");
            if (t.enabled && ((!t.targetLevel && t.constructionLevel < 0) || !(t.unitMask & s.admittedUnitMask) || b.maxUnitInside == 0)) fail(b.key, "training has no valid target or occupants");
        }
        range(s.production.fallbackUnit, 0, NB_UNIT_TYPE - 1, b.key + ".fallbackUnit");
        const BuildingProductionRecipe* first = nullptr;
        for (int u = 0; u < NB_UNIT_TYPE; ++u)
        {
            const auto& recipe = s.production.recipes[u];
            range(recipe.duration, 0, 1000000, b.key + ".recipe.duration");
            range(s.production.initialRatios[u], 0, 32767, b.key + ".initialRatios");
            if (!recipe.enabled) continue;
            if (first && s.production.scheduling == BuildingProductionScheduling::WeightedLateChoice &&
                (recipe.duration != first->duration || recipe.cost != first->cost)) fail(b.key, "late-choice production requires equal recipes");
            first = &recipe;
        }
        if (first && s.production.scheduling == BuildingProductionScheduling::WeightedLateChoice &&
            !s.production.recipes[s.production.fallbackUnit].enabled) fail(b.key, "late-choice fallback recipe is disabled");
        if (s.repairable && (b.prevLevel < 0 || !(*entries_)[b.prevLevel].isBuildingSite))
            fail(b.key, "repair requires a construction variant");
        if (!b.isBuildingSite && b.nextLevel >= 0 && !(*entries_)[b.nextLevel].isBuildingSite)
            fail(b.key, "upgrade must target a construction variant; use a zero-cost site for instant upgrades");
        if (b.isBuildingSite && (b.nextLevel < 0 || (*entries_)[b.nextLevel].isBuildingSite))
            fail(b.key, "construction requires a completed result variant");
        if (s.placeable && !b.isBuildingSite && !s.instantPlacement)
            fail(b.key, "placeable completed variant requires instant placement");
        range(s.market.pickupPenalty, 0, 1000000, b.key + ".pickupPenalty");
        range(s.ammunitionResource, 0, MAX_RESOURCES - 1, b.key + ".ammunitionResource");
        range(s.ammunitionCost, 0, 1000000, b.key + ".ammunitionCost");
        for (const auto damage : s.projectileDamage) range(damage, 0, 1000000, b.key + ".projectileDamage");
        range(s.projectileBuildingDamage, 0, 1000000, b.key + ".projectileBuildingDamage");
        range(b.shootingRange, 0, 1024, b.key + ".shootingRange");
        if (b.shootingRange)
        {
            range(b.shootSpeed, 256, 65535, b.key + ".shootSpeed");
            range(b.shootRhythm, 1, 65535, b.key + ".shootRhythm");
            range(b.multiplierStoneToBullets, 1, 1000000, b.key + ".multiplierStoneToBullets");
            range(b.maxBullets, b.multiplierStoneToBullets, 1000000, b.key + ".maxBullets");
        }
    }
    startingBuildingId_ = startingBuildingKey_.empty() ? -1 : findByKey(startingBuildingKey_);
    if (!startingBuildingKey_.empty() && startingBuildingId_ < 0) fail("startingBuilding", "unresolved stable key");
    for (auto& b : *entries_) b.terminalTypeNum = -1;
    // Forward progression must terminate. Previous links deliberately include
    // the repair edge and therefore are not part of cycle detection.
    std::vector<unsigned char> visited(entries_->size());
    for (std::size_t start = 0; start < entries_->size(); ++start)
    {
        int at = static_cast<int>(start);
        while (at >= 0 && visited[at] == 0) { visited[at] = 1; at = (*entries_)[at].nextLevel; }
        if (at >= 0 && visited[at] == 1) fail((*entries_)[at].key, "cyclic forward transitions");
        at = static_cast<int>(start);
        while (at >= 0 && visited[at] == 1) { visited[at] = 2; at = (*entries_)[at].nextLevel; }
    }
    // Resolve each chain once. Entity extraction reads one cached integer.
    std::vector<Sint32> path;
    for (std::size_t start=0; start<entries_->size(); ++start)
    {
        Sint32 at=static_cast<Sint32>(start);
        path.clear();
        while ((*entries_)[at].terminalTypeNum < 0)
        {
            path.push_back(at);
            if ((*entries_)[at].nextLevel < 0) break;
            at=(*entries_)[at].nextLevel;
        }
        const Sint32 terminal=(*entries_)[at].terminalTypeNum >= 0 ? (*entries_)[at].terminalTypeNum : at;
        for (Sint32 id : path) (*entries_)[id].terminalTypeNum=terminal;
    }

    compileRuntimeTraits();

}
