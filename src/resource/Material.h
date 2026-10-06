// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <string>

// Material identities describe inventory and engine capabilities. They are not
// map resource identities. Preserve these positions when extending save formats.
enum class MaterialId : std::uint8_t
{
    Wood = 0, Food, Paper, Stone, Algae, Cherries, Oranges, Prunes,
    Gold, Metal, Glass, Fabric
};
inline constexpr unsigned MaterialCount = 12;
using MaterialMask = std::uint16_t;
inline constexpr MaterialMask AllMaterials = (MaterialMask{1} << MaterialCount) - 1;
constexpr unsigned materialIndex(MaterialId material) { return static_cast<unsigned>(material); }
constexpr bool validMaterial(unsigned material) { return material < MaterialCount; }
constexpr MaterialMask materialBit(MaterialId material)
{
    return validMaterial(materialIndex(material)) ? MaterialMask(1u << materialIndex(material)) : 0;
}
inline constexpr std::array<std::string_view, MaterialCount> MaterialKeys = {
    "wood", "food", "paper", "stone", "algae", "cherries", "oranges", "prunes",
    "gold", "metal", "glass", "fabric"
};
constexpr std::string_view materialKey(MaterialId material)
{
    return validMaterial(materialIndex(material)) ? MaterialKeys[materialIndex(material)] : std::string_view{};
}
constexpr std::optional<MaterialId> parseMaterialKey(std::string_view key)
{
    for (unsigned i = 0; i < MaterialCount; ++i)
        if (MaterialKeys[i] == key) return static_cast<MaterialId>(i);
    return std::nullopt;
}
std::string getMaterialName(int material);
inline std::string getMaterialName(MaterialId material) { return getMaterialName(int(materialIndex(material))); }
