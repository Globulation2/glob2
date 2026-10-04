// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace Online
{
// Swarm meshes a colony skin can choose. The id is part of the signed skin
// manifest and the platform contract (platform/packages/protocol/src/skins.ts);
// the file ships in data/skins/colony-v1. Index 0 is the classic swarm, which
// every skin published before mesh choice uses.
struct SwarmMesh
{
    std::string_view id, file;
};
inline constexpr std::array<SwarmMesh, 7> SWARM_MESHES{{
    {"classic", "swarm.gsk"},
    {"crown", "swarm-crown.gsk"},
    {"clutch", "swarm-clutch.gsk"},
    {"toadstool", "swarm-toadstool.gsk"},
    {"coral", "swarm-coral.gsk"},
    {"skep", "swarm-skep.gsk"},
    {"bloom", "swarm-bloom.gsk"},
}};
// Index into SWARM_MESHES, or -1 for an id this client does not know.
inline int swarmMeshIndex(std::string_view id)
{
    for (std::size_t i = 0; i < SWARM_MESHES.size(); ++i)
        if (SWARM_MESHES[i].id == id) return static_cast<int>(i);
    return -1;
}
}
