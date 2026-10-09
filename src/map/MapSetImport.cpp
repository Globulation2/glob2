// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapAssetBundle.h"
#include "Map.h"
#include "Game.h"
#include "render/terrain/TerrainCatalogIO.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include "online/Sha256.h"

void Map::importSet(std::string_view source, const std::vector<std::string>& selected)
{
    if (game && !game->edit) throw std::logic_error("Sets can only change in the map editor");
    if (source.size() > MapAssetBundle::MaximumBytes) throw std::invalid_argument("Set exceeds 16 MiB");
    using Json = nlohmann::json;
    auto package = MapAssetBundle::parseDocument(source);
    const std::set<std::string> allowed{"schemaVersion", "setId", "versionId", "title", "description", "tags", "license", "credits", "terrains", "resources", "experiments", "assets"};
    if (!package.is_object()) throw std::invalid_argument("Expected a set package");
    for (const auto& [key, value] : package.items())
        if (!allowed.contains(key)) throw std::invalid_argument("Unknown set field: " + key);
    auto compactUuid = [](const Json& value) {
        auto id = value.get<std::string>();
        if (id.size() != 36) throw std::invalid_argument("Invalid set identity");
        std::string result;
        for (unsigned i = 0; i < id.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (id[i] != '-') throw std::invalid_argument("Invalid set identity");
            } else {
                if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
                    throw std::invalid_argument("Invalid set identity");
                result += id[i];
            }
        }
        return result;
    };
    if (package.at("schemaVersion") != 1) throw std::invalid_argument("Unsupported set schema");
    const auto prefix = "s" + compactUuid(package.at("setId")) + compactUuid(package.at("versionId")) + ":";
    const auto license = package.at("license").get<std::string>();
    if (license != "CC0-1.0" && license != "CC-BY-4.0") throw std::invalid_argument("Unsupported set license");
    auto boundedText = [](const Json& value, unsigned limit, bool required) {
        if (!value.is_string() || std::count_if(value.get_ref<const std::string&>().begin(), value.get_ref<const std::string&>().end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }) > limit ||
            (required && value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos))
            throw std::invalid_argument("Invalid set metadata");
    };
    boundedText(package.at("title"), 128, true); boundedText(package.at("description"), 2000, false);
    if (!package.at("tags").is_array() || package.at("tags").size() > 8) throw std::invalid_argument("Invalid set tags");
    for (const auto& tag : package.at("tags")) boundedText(tag, 32, true);
    if (!package.at("credits").is_array() || package.at("credits").empty() || package.at("credits").size() > 64)
        throw std::invalid_argument("Invalid set credits");
    for (const auto& credit : package.at("credits")) {
        if (!credit.is_object()) throw std::invalid_argument("Invalid set credit");
        for (const auto& [key, value] : credit.items()) if (key != "author" && key != "source" && key != "license") throw std::invalid_argument("Unknown credit field");
        boundedText(credit.at("author"), 128, true);
        if (credit.contains("source")) boundedText(credit.at("source"), 2000, false);
        if (credit.at("license") != "CC0-1.0" && credit.at("license") != "CC-BY-4.0") throw std::invalid_argument("Unsupported contributor license");
        if (license == "CC0-1.0" && credit.at("license") != "CC0-1.0") throw std::invalid_argument("CC-BY artwork cannot be relicensed as CC0");
    }
    for (const auto* entries : {&package.at("terrains"), &package.at("resources")})
        if (!entries->is_array() || entries->size() > 16384) throw std::invalid_argument("Too many set entries");
    std::set<std::string> entryKeys;
    for (const auto* entries : {&package.at("terrains"), &package.at("resources")})
        for (const auto& entry : *entries) {
            const auto key = entry.at("key").get<std::string>();
            if (!key.starts_with(prefix) || !entryKeys.insert(key).second)
                throw std::invalid_argument("Set entries need distinct keys in the set and release namespace");
        }
    if (!selected.empty()) {
        std::set<std::string> keep(selected.begin(), selected.end()), known;
        for (const auto* entries : {&package.at("terrains"), &package.at("resources")})
            for (const auto& entry : *entries) known.insert(entry.at("key").get<std::string>());
        for (const auto& key : keep) if (!known.contains(key)) throw std::invalid_argument("Unknown selected set entry");
        for (const auto& entry : package.at("terrains")) {
            if (!keep.contains(entry.at("key").get<std::string>())) continue;
            if (entry.contains("allowedResourceKeys") && entry.at("allowedResourceKeys").is_array())
                for (const auto& key : entry.at("allowedResourceKeys"))
                    if (known.contains(key.get<std::string>())) keep.insert(key.get<std::string>());
        }
        for (auto* entries : {&package.at("terrains"), &package.at("resources")})
            for (auto it = entries->begin(); it != entries->end();) {
                if (!keep.contains(it->at("key").get<std::string>())) it = entries->erase(it); else ++it;
            }
        auto& bindings = package.at("assets").at("terrains");
        for (auto it = bindings.begin(); it != bindings.end();) {
            if (!keep.contains(it.key())) it = bindings.erase(it); else ++it;
        }
        std::set<std::string> sheets;
        for (const auto& [key, material] : bindings.items()) {
            sheets.insert(material.at("sprite").get<std::string>());
            if (material.contains("decor")) sheets.insert(material.at("decor").at("sprite").get<std::string>());
        }
        for (const auto& entry : package.at("resources")) sheets.insert(entry.at("presentation").at("sprite").get<std::string>());
        auto& images = package.at("assets").at("sheets");
        for (auto it = images.begin(); it != images.end();) {
            if (!sheets.contains("data/sets/" + it->at("hash").get<std::string>())) it = images.erase(it); else ++it;
        }
    }
    // Previously copied dependencies keep their map-local changes. Explicitly
    // selecting an existing entry is rejected instead of silently overwriting it.
    for (auto* entries : {&package.at("terrains"), &package.at("resources")})
        for (auto it=entries->begin();it!=entries->end();) {
            const auto key=it->at("key").get<std::string>();
            if(terrainRegistry().find(key)||resourceRegistry().find(key)) {
                if(selected.empty()||std::find(selected.begin(),selected.end(),key)!=selected.end())
                    throw std::invalid_argument("This entry is already imported. Edit the local copy or explicitly update to another release.");
                it=entries->erase(it);
            } else ++it;
        }
    auto terrains = package.at("terrains"), resources = package.at("resources");
    if (!terrains.is_array() || !resources.is_array() || (terrains.empty() && resources.empty()))
        throw std::invalid_argument("A set needs terrain or resources");
    for (const auto* entries : {&terrains, &resources})
        for (const auto& entry : *entries)
            if (!entry.at("key").get<std::string>().starts_with(prefix))
                throw std::invalid_argument("Entry key must use the set and release namespace");
    // Built-in looks may be referenced, but arbitrary installed files are not
    // part of a portable release. Authors upload sheets for new artwork.
    std::map<std::string,unsigned> builtinFrames;
    const auto builtins=ResourceRegistry::builtins();
    for(unsigned id=0;id<builtins->size();++id) {
        const auto& p=builtins->presentation(static_cast<ResourceId>(id));
        for(const auto& level:p.levels)for(const auto& v:level.variants)
            builtinFrames[p.sprite]=std::max(builtinFrames[p.sprite],unsigned(v.frame)+(p.animationFrames-1)*p.animationStride+1);
    }
    for(const auto& entry:resources) {
        const auto& p=entry.at("presentation");const auto path=p.at("sprite").get<std::string>();
        if(path.starts_with("data/sets/"))continue;
        const auto found=builtinFrames.find(path);
        if(found==builtinFrames.end())throw std::invalid_argument("Set resource artwork must use a bundled sheet or an installed resource preset");
        for(const auto& level:p.at("levels"))for(const auto& v:level.at("variants")) {
            const auto frame=v.at("frame").get<std::int64_t>();
            const auto frames=p.value("animationFrames",std::int64_t(1)),stride=p.value("animationStride",std::int64_t(1));
            if(frame<0||frames<1||frames>256||stride<1||stride>65535||frame+(frames-1)*stride>=found->second)
                throw std::invalid_argument("Resource references a missing preset frame");
        }
    }
    auto assetsDocument = package.at("assets");
    assetsDocument["credits"] = Json::array({Json{{"setId", package.at("setId")},
        {"versionId", package.at("versionId")}, {"title", package.at("title")},
        {"license", license}, {"authors", package.at("credits")}, {"sourceHash", Online::Sha256::hex(source)},
        {"entries", Json::array()}}});
    for (const auto* entries : {&terrains, &resources})
        for (const auto& entry : *entries) assetsDocument["credits"][0]["entries"].push_back(entry.at("key"));
    auto incoming = MapAssetBundle::deserialize(assetsDocument.dump());
    for (const auto& [key, material] : incoming->terrains.items()) {
        if (std::none_of(terrains.begin(), terrains.end(), [&](const Json& entry) { return entry.at("key") == key; }))
            throw std::invalid_argument("Artwork binding has no terrain definition");
    }
    auto assets = assetBundleValue->merge(*incoming);
    auto terrain = terrainRegistry().importJson(Json{{"schemaVersion", 1}, {"terrains", terrains}}.dump());
    auto resource = resourceRegistry().importJson(Json{{"schemaVersion", 1}, {"resources", resources},
        {"experiments", package.value("experiments", Json::array())}}.dump());
    assets->validate(*terrain, *resource);
    installCatalogs(std::move(terrain), std::move(resource), std::move(assets));
}

void Map::editCustomEntry(std::string_view key, std::string_view definition, std::string_view artwork)
{
    if (game && !game->edit) throw std::logic_error("Custom content can only change in the map editor");
    auto entry = MapAssetBundle::parseDocument(definition);
    if (entry.at("key") != key || key.starts_with("glob2:")) throw std::invalid_argument("Cannot change a built-in or an entry identity");
    auto terrain=terrainRegistryValue; auto resource=resourceRegistryValue;
    auto assetsDocument=MapAssetBundle::parseDocument(assetBundleValue->serialize());
    if (auto id=terrainRegistry().find(key); id && unsigned(*id)>=TERRAIN_COUNT) {
        terrain=terrainRegistry().importJson(nlohmann::json{{"schemaVersion",1},{"terrains",nlohmann::json::array({entry})}}.dump());
        auto material=MapAssetBundle::parseDocument(artwork);
        if (material.is_null()) assetsDocument["terrains"].erase(std::string(key));
        else assetsDocument["terrains"][std::string(key)]=std::move(material);
    } else if (auto id=resourceRegistry().find(key); id && std::string(resourceRegistry().key(*id)).find(':')!=std::string::npos) {
        resource=resourceRegistry().importJson(nlohmann::json{{"schemaVersion",1},{"resources",nlohmann::json::array({entry})}}.dump());
    } else throw std::invalid_argument("Unknown custom entry");
    auto assets=MapAssetBundle::deserialize(assetsDocument.dump()); assets->validate(*terrain,*resource);
    installCatalogs(std::move(terrain),std::move(resource),std::move(assets));
}

void Map::updateSet(std::string_view source, std::string_view oldVersion, const std::vector<std::string>& selected)
{
    if(game && !game->edit)throw std::logic_error("Sets can only change in the map editor");
    const auto package=MapAssetBundle::parseDocument(source);
    const auto& credits=assetBundleValue->credits;
    auto previous=std::find_if(credits.begin(),credits.end(),[&](const auto& c){return c.at("versionId").template get<std::string>()==oldVersion && c.at("setId")==package.at("setId");});
    if(previous==credits.end() || oldVersion==package.at("versionId").get<std::string>()) throw std::invalid_argument("Choose a different release of an imported set");
    std::map<TerrainType,std::string> oldTerrain;
    std::map<ResourceId,std::string> oldResources;
    for(const auto& key:previous->at("entries")) {
        const auto name=key.get<std::string>();const auto suffix=name.substr(name.find(':')+1);
        if(auto id=terrainRegistry().find(name)) oldTerrain[*id]=suffix;
        if(auto id=resourceRegistry().find(name)) oldResources[*id]=suffix;
    }
    std::set<std::string> known, keep(selected.begin(),selected.end());
    for(const auto* entries:{&package.at("terrains"),&package.at("resources")})
        for(const auto& entry:*entries)known.insert(entry.at("key").get<std::string>());
    if(keep.empty())keep=known;
    for(const auto& key:keep)if(!known.contains(key))throw std::invalid_argument("Unknown selected set entry");
    for(const auto& entry:package.at("terrains"))
        if(keep.contains(entry.at("key").get<std::string>()) && entry.contains("allowedResourceKeys") && entry.at("allowedResourceKeys").is_array())
            for(const auto& key:entry.at("allowedResourceKeys"))if(known.contains(key.get<std::string>()))keep.insert(key.get<std::string>());
    const auto sourceHash=Online::Sha256::hex(source);
    std::vector<std::string> missing;
    for(const auto& key:keep) {
        if(!terrainRegistry().find(key) && !resourceRegistry().find(key))missing.push_back(key);
        else if(std::none_of(credits.begin(),credits.end(),[&](const auto& credit) {
            return credit.at("setId")==package.at("setId") && credit.at("versionId")==package.at("versionId") &&
                credit.at("sourceHash")==sourceHash && std::find(credit.at("entries").begin(),credit.at("entries").end(),key)!=credit.at("entries").end();
        }))throw std::invalid_argument("Existing entry does not belong to this exact release");
    }
    if(!missing.empty())importSet(source,missing);
    auto compact=[](std::string value){std::erase(value,'-');return value;};
    const auto prefix="s"+compact(package.at("setId").get<std::string>())+compact(package.at("versionId").get<std::string>())+":";
    auto edit=editTerrain();
    for(size_t i=0;i<cellCount();++i) {
        const int x=int(i&wMask), y=int(i>>wDec);
        // Terrain lives on vertices; vertex i is the top-left corner of cell i.
        if(auto old=oldTerrain.find(vertexTerrain[i]);old!=oldTerrain.end())
            if(keep.contains(prefix+old->second))if(auto next=terrainRegistry().find(prefix+old->second)) setVertexTerrain(x,y,*next);
        auto r=resourceCells[i].resource;
        if(auto old=oldResources.find(static_cast<ResourceId>(r.type));old!=oldResources.end())
            if(keep.contains(prefix+old->second))if(auto next=resourceRegistry().find(prefix+old->second)) {
                const auto stock=materialStocksAt(i);r.type=resourceIndex(*next);replaceResource(i,r);
                // Install surviving stocks before clearing initial stocks. An
                // intermediate empty total must not destroy a mixed deposit.
                for(unsigned m=0;m<MaterialCount;++m)if(stock[m])setMaterialAmountSlot(i,m,stock[m]);
                for(unsigned m=0;m<MaterialCount;++m)if(!stock[m])setMaterialAmountSlot(i,m,0);
            }
    }
}
