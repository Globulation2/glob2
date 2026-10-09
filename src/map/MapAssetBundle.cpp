// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapAssetBundle.h"
#include "online/Sha256.h"
#include "TerrainRegistry.h"
#include "ResourceRegistry.h"
#include "render/terrain/TerrainCatalogIO.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <algorithm>
#include <set>

namespace {
using Json = nlohmann::json;
[[noreturn]] void fail(const char* message) { throw std::invalid_argument(message); }
void text(const Json& value, unsigned maximum, bool required = false) {
    if (!value.is_string()) fail("Invalid artwork attribution");
    const auto& string=value.get_ref<const std::string&>();
    if (std::count_if(string.begin(), string.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }) > maximum ||
        (required && string.find_first_not_of(" \t\r\n") == std::string::npos)) fail("Invalid artwork attribution");
}
void uuid(const Json& value) {
    text(value, 36, true);
    const auto& id=value.get_ref<const std::string&>();
    if(id.size()!=36) fail("Invalid artwork identity");
    for(unsigned i=0;i<36;++i)
        if(i==8||i==13||i==18||i==23) { if(id[i]!='-')fail("Invalid artwork identity"); }
        else if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))fail("Invalid artwork identity");
}
void fields(const Json& value, std::initializer_list<const char*> allowed) {
    if (!value.is_object()) fail("Expected an asset object");
    for (const auto& [key, unused] : value.items())
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* field) { return key == field; }))
            fail("Unknown asset field");
}
unsigned integer(const Json& value, const char* key, unsigned maximum) {
    const auto& n = value.at(key);
    if (!n.is_number_integer() || n < 1 || n > maximum) fail("Invalid spritesheet dimension");
    return n.get<unsigned>();
}
std::string decode(std::string_view input) {
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve(input.size() * 3 / 4);
    unsigned accumulator = 0, bits = 0;
    for (char c : input) {
        auto digit = alphabet.find(c);
        if (digit == alphabet.npos) fail("Invalid base64url spritesheet");
        accumulator = (accumulator << 6) | unsigned(digit); bits += 6;
        if (bits >= 8) { bits -= 8; result.push_back(char(accumulator >> bits)); }
    }
    if (bits >= 6 || (bits && (accumulator & ((1u << bits) - 1)))) fail("Noncanonical spritesheet encoding");
    return result;
}
unsigned bigEndian(const std::string& bytes, unsigned offset) {
    unsigned value = 0;
    for (unsigned i = offset; i < offset + 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[i]);
    return value;
}
}
nlohmann::json MapAssetBundle::parseDocument(std::string_view bytes) {
    if (bytes.size() > MaximumBytes) fail("Custom content exceeds 16 MiB");
    std::vector<std::set<std::string>> objects;
    return Json::parse(bytes, [&objects](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 24) fail("Custom content is nested too deeply");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        else if (event == Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
            fail("Duplicate custom content field");
        else if (event == Json::parse_event_t::object_end) objects.pop_back();
        return true;
    });
}
std::shared_ptr<const MapAssetBundle> MapAssetBundle::empty() {
    static auto value = std::make_shared<const MapAssetBundle>();
    return value;
}
std::shared_ptr<const MapAssetBundle> MapAssetBundle::deserialize(std::string_view bytes) {
    if (bytes.size() > MaximumBytes) fail("Custom artwork exceeds 16 MiB");
    auto result = std::make_shared<MapAssetBundle>();
    result->document = parseDocument(bytes);
    const auto& root = result->document;
    fields(root, {"schemaVersion", "sheets", "terrains", "credits"});
    if (root.at("schemaVersion") != 1 || !root.at("sheets").is_array() ||
        !root.at("terrains").is_object() || !root.at("credits").is_array()) fail("Invalid artwork bundle");
    if (root.at("sheets").size() > 256 || root.at("terrains").size() > 16384 || root.at("credits").size() > 256)
        fail("Too many custom assets");
    for (const auto& credit : root.at("credits")) {
        fields(credit, {"setId", "versionId", "title", "license", "authors", "sourceHash", "entries"});
        if (credit.at("license") != "CC0-1.0" && credit.at("license") != "CC-BY-4.0") fail("Unsupported artwork license");
        uuid(credit.at("setId")); uuid(credit.at("versionId")); text(credit.at("title"),128,true);
        text(credit.at("sourceHash"),64,true);
        if(!Online::Sha256::isHexDigest(credit.at("sourceHash").get<std::string>()))fail("Invalid artwork source hash");
        if(!credit.at("entries").is_array()||credit.at("entries").size()>32768)fail("Invalid artwork entries");
        for(const auto& entry:credit.at("entries"))if(!entry.is_string()||entry.get_ref<const std::string&>().size()>128)fail("Invalid credited entry");
        for(const auto& author:credit.at("authors")) {
            fields(author,{"author","source","license"});
            text(author.at("author"),128,true);
            if(author.at("license")!="CC0-1.0"&&author.at("license")!="CC-BY-4.0")fail("Invalid contributor license");
            if(author.contains("source"))text(author.at("source"),2000);
        }
        if (!credit.at("authors").is_array() || credit.at("authors").empty() || credit.at("authors").size() > 64) fail("Invalid artwork credits");
    }
    std::size_t decoded = 0;
    for (const auto& item : root.at("sheets")) {
        fields(item, {"hash", "png", "frameWidth", "frameHeight"});
        const auto hash = item.at("hash").get<std::string>();
        if (!Online::Sha256::isHexDigest(hash)) fail("Invalid sheet hash");
        Sheet sheet;
        sheet.png = decode(item.at("png").get<std::string>());
        if (sheet.png.size() < 33 || sheet.png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) ||
            sheet.png.compare(12, 4, "IHDR") || bigEndian(sheet.png, 8) != 13 ||
            Online::Sha256::hex(sheet.png) != hash) fail("Invalid PNG or spritesheet hash");
        sheet.width = bigEndian(sheet.png, 16); sheet.height = bigEndian(sheet.png, 20);
        sheet.frameWidth = integer(item, "frameWidth", 64);
        sheet.frameHeight = integer(item, "frameHeight", 64);
        if (!sheet.width || !sheet.height || sheet.width > 2048 || sheet.height > 2048 ||
            sheet.width % sheet.frameWidth || sheet.height % sheet.frameHeight || sheet.frames() > 65536)
            fail("Spritesheet does not fit its frame grid");
        decoded += std::size_t(sheet.width) * sheet.height * 4;
        if (decoded > MaximumDecodedBytes) fail("Custom artwork exceeds 64 MiB of pixels");
        auto* io = SDL_IOFromConstMem(sheet.png.data(), sheet.png.size());
        if (!io) fail("Cannot read spritesheet");
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(IMG_LoadPNG_IO(io), SDL_DestroySurface);
        SDL_CloseIO(io);
        if (!surface || surface->w != int(sheet.width) || surface->h != int(sheet.height))
            fail("Cannot decode PNG spritesheet");
        if (!result->sheets.emplace("data/sets/" + hash, std::move(sheet)).second) fail("Duplicate spritesheet");
    }
    result->terrains = root.at("terrains"); result->credits = root.at("credits");
    for (const auto& [key, material] : result->terrains.items()) {
        if (key.size() > 128 || key.find(':') == std::string::npos || key.starts_with("glob2:"))
            fail("Artwork bindings must name custom terrain");
        fields(material, {"sprite", "profile", "edges", "decor", "animation_frames", "animation_ticks", "animation_stride", "preview", "minimap", "seam", "variants"});
        if (material.contains("decor")) fields(material.at("decor"), {"sprite", "full", "edge"});
        if (material.contains("seam")) fields(material.at("seam"), {"height", "cast_q8", "cast_width_q8", "fringe_q8", "fringe_width_q8", "fringe"});
        if (!material.at("variants").is_array() || material.at("variants").size() > 256) fail("Too many terrain variants");
        for (const auto& variant : material.at("variants")) fields(variant, {"frame", "weight"});
        const auto sprite = material.at("sprite").get<std::string>();
        const auto found = result->sheets.find(sprite);
        if (found == result->sheets.end() || found->second.frameWidth != 32 || found->second.frameHeight != 32)
            fail("Terrain artwork requires a bundled 32x32 sheet");
    }
    return result;
}
std::string MapAssetBundle::serialize() const { return document.dump(); }
void MapAssetBundle::validate(const TerrainRegistry& terrain, const ResourceRegistry& resources) const {
    for (const auto& [key, unused] : terrains.items())
        if (!terrain.find(key) || unsigned(*terrain.find(key)) < TERRAIN_COUNT) fail("Unknown custom terrain artwork binding");
    if (!terrains.empty()) {
        // Parse against the engine's boundary families without creating a graphics context.
        const auto visuals = TerrainVisual::loadCatalog(std::make_shared<const MapAssetBundle>(*this));
        for (const auto& material : visuals.materials) {
            const auto found = sheets.find(material.sprite);
            if (!material.decor.sprite.empty() && terrains.contains(material.key)) {
                const auto decor = sheets.find(material.decor.sprite);
                if (decor == sheets.end()) fail("Custom decor requires a bundled spritesheet");
                for (const auto& frames : {material.decor.full, material.decor.edge})
                    for (int frame : frames) if (frame >= int(decor->second.frames())) fail("Missing custom decor frame");
            }
            if (found == sheets.end()) continue;
            for (const auto& variant : material.variants)
                if (variant.frame + (material.animationFrames - 1) * material.animationStride >= int(found->second.frames()))
                    fail("Terrain references a missing spritesheet frame");
        }
    }
    std::set<std::string> portableEntries;
    for (const auto& credit : credits)
        for (const auto& entry : credit.at("entries")) portableEntries.insert(entry.get<std::string>());
    for (unsigned id = 0; id < resources.size(); ++id) {
        const auto& presentation = resources.presentation(static_cast<ResourceId>(id));
        if (!presentation.sprite.starts_with("data/sets/")) continue;
        const auto found = sheets.find(presentation.sprite);
        if (found == sheets.end()) {
            if (portableEntries.contains(std::string(resources.key(static_cast<ResourceId>(id)))))
                fail("Resource spritesheet is missing");
            // Older manually imported definitions may name installed files in
            // any data/ directory. Retain that meaning when they are resaved.
            continue;
        }
        for (const auto& level : presentation.levels)
            for (const auto& variant : level.variants)
                if (variant.frame + (presentation.animationFrames - 1) * presentation.animationStride >= found->second.frames())
                    fail("Resource references a missing spritesheet frame");
    }
}
std::shared_ptr<const MapAssetBundle> MapAssetBundle::merge(const MapAssetBundle& other) const {
    auto combined = document;
    for (const auto& item : other.document.at("sheets")) {
        auto& sheets = combined["sheets"];
        auto found = std::find_if(sheets.begin(), sheets.end(), [&](const Json& old) { return old.at("hash") == item.at("hash"); });
        if (found == sheets.end()) sheets.push_back(item);
        else if (*found != item) fail("Conflicting sheet layouts");
    }
    for (const auto& [key, value] : other.terrains.items()) combined["terrains"][key] = value;
    for (const auto& credit : other.credits) {
        auto& credits = combined["credits"];
        auto existing=std::find_if(credits.begin(),credits.end(),[&](const Json& old) {
            return old.at("setId")==credit.at("setId") && old.at("versionId")==credit.at("versionId");
        });
        if(existing==credits.end()) credits.push_back(credit);
        else {
            auto old=*existing, incoming=credit;
            old.erase("entries"); incoming.erase("entries");
            if(old!=incoming) fail("Conflicting set release attribution");
            auto& entries=existing->at("entries");
            for(const auto& entry:credit.at("entries"))
                if(std::find(entries.begin(),entries.end(),entry)==entries.end())entries.push_back(entry);
        }
    }
    return deserialize(combined.dump());
}
