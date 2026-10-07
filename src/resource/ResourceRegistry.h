// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ResourceProperties.h"
#include "ExperimentalFeatures.h"
#include <cassert>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ResourceSpriteVariant
{
    std::uint16_t frame = 0;
    std::uint32_t weight = 1;
    bool operator==(const ResourceSpriteVariant&) const = default;
};
struct ResourceSpriteLevel
{
    std::uint32_t stock = 0;
    std::vector<ResourceSpriteVariant> variants;
    bool operator==(const ResourceSpriteLevel&) const = default;
};
struct ResourcePresentation
{
    std::string name, sprite;
    std::array<std::uint8_t, 3> minimap{};
    std::vector<ResourceSpriteLevel> levels;
    std::uint16_t animationFrames = 1, animationStride = 1;
    std::uint32_t animationTicks = 1;
    // Stable across registry reorderings and independent of simulation RNG.
    std::uint32_t visualSeed = 0;
    std::uint16_t frame(std::uint32_t stock, int x, int y, std::uint32_t tick = 0) const;
};

// Resolved immutable resource definitions travel with the map. Factory calls
// publish only complete validated snapshots; readers can retain older snapshots.
class ResourceRegistry
{
public:
    static constexpr unsigned Capacity = 16384;
    static constexpr std::size_t MaximumDefinitionBytes = 32 * 1024 * 1024;
    static std::shared_ptr<const ResourceRegistry> builtins();
    static std::shared_ptr<const ResourceRegistry> legacy();
    static std::shared_ptr<const ResourceRegistry> loadDefaultsFile(const std::string& path);
    // Startup may proceed to embedded maps without any installed default catalog.
    static std::shared_ptr<const ResourceRegistry> empty();
    static std::shared_ptr<const ResourceRegistry> availableDefaults();
    static std::shared_ptr<const ResourceRegistry> loadFile(const std::string& path);
    static std::shared_ptr<const ResourceRegistry> fromJson(std::string_view source);
    static std::shared_ptr<const ResourceRegistry> deserialize(std::string_view source);
    std::shared_ptr<const ResourceRegistry> importJson(std::string_view source) const;
    std::string serialize() const;
    std::size_t size() const { return properties_.size(); }
    bool valid(unsigned id) const { return id < size(); }
    bool valid(ResourceId id) const { return valid(resourceIndex(id)); }
    std::optional<ResourceId> find(std::string_view key) const;
    const std::string& key(ResourceId id) const { assert(valid(id)); return keys_[resourceIndex(id)]; }
    const ResourceProperties& properties(ResourceId id) const { assert(valid(id)); return properties_[resourceIndex(id)]; }
    const ResourceYields& yields(ResourceId id) const { assert(valid(id)); return yields_[resourceIndex(id)]; }
    const ResourcePresentation& presentation(ResourceId id) const { assert(valid(id)); return presentations_[resourceIndex(id)]; }
    const std::string& requiredExperiment(ResourceId id) const { assert(valid(id)); return requiredExperiments_[resourceIndex(id)]; }
    const std::vector<CatalogExperimentDefinition>& experiments() const { return experiments_; }
    std::vector<std::string> experimentKeys() const;
    // Conservative intrinsic source mutability, across all definitions (including
    // currently unplaced ones). Explicit map edits are tracked independently.
    MaterialMask mutableMaterialSources() const { return mutableMaterialSources_; }
    const std::vector<ResourceProperties>& propertyTable() const { return properties_; }
    const std::string& digest() const { return digest_; }
    std::uint32_t checksum() const { return checksum_; }
private:
    ResourceRegistry() = default;
    ResourceRegistry(const ResourceRegistry&) = default;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;
    void compile();
    std::vector<std::string> keys_, requiredExperiments_;
    std::map<std::string, ResourceId, std::less<>> keyIndex_;
    std::vector<ResourceProperties> properties_;
    std::vector<ResourceYields> yields_;
    std::vector<ResourcePresentation> presentations_;
    std::vector<CatalogExperimentDefinition> experiments_;
    std::string digest_;
    std::uint32_t checksum_ = 0;
    MaterialMask mutableMaterialSources_ = 0;
};
