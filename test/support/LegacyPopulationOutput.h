// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "FileFormatVersions.h"
#include <BinaryStream.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace glob2test
{
// Test-only adapter for synthetic historical files. Header overrides alone do
// not turn current counted population records into the pre-165 slot encoding.
// Other historical field differences remain the fixture author's responsibility.
class LegacyPopulationOutput : public GAGCore::BinaryOutputStream
{
    Game* game;
    bool legacy;
    bool injecting = false;
    std::vector<std::string> sections;
    std::size_t suppressedAt = 0;
    int nextTeam = 0;
    Team* team = nullptr;
    unsigned populationSection = 0;

    bool suppressed() const { return suppressedAt && !injecting; }
    void population(const std::string& name)
    {
        if (!team || populationSection >= 4)
            throw std::logic_error("Unexpected synthetic historical population section");
        const bool units = name == "myUnits";
        const bool crossReferences = populationSection >= 2;
        if (units != (populationSection % 2 == 0))
            throw std::logic_error("Unexpected synthetic historical population order");
        injecting = true;
        // These capacity walks deliberately implement the historical file layout.
        const int capacity = units ? Unit::MAX_COUNT : Building::MAX_COUNT;
        for (int id=0; id<capacity; ++id) {
            if (!crossReferences) writeUint32(units ? team->myUnits[id]!=nullptr
                                                    : team->myBuildings[id]!=nullptr, "isUsed");
            if (units) {
                if (auto* entity=team->myUnits[id]) {
                    if (crossReferences) entity->saveCrossRef(this);
                    else entity->save(this);
                }
            } else if (auto* entity=team->myBuildings[id]) {
                if (crossReferences) entity->saveCrossRef(this);
                else entity->save(this);
            }
        }
        injecting = false;
        ++populationSection;
        suppressedAt = sections.size();
    }
public:
    LegacyPopulationOutput(GAGCore::MemoryStreamBackend* backend, Game* game, int version)
        : BinaryOutputStream(backend), game(game), legacy(version<FILE_FORMAT_VERSION_INTRUSIVE_POPULATION) {}
    void writeEnterSection(const std::string name) override
    {
        if (injecting || !legacy) return;
        const bool teamChild = !sections.empty() && sections.back()=="Team";
        sections.push_back(name);
        if (suppressed()) return;
        if (name=="Team") {
            if (!game || nextTeam>=game->teamsCount())
                throw std::logic_error("Unexpected synthetic historical team");
            team=game->teams[nextTeam++];
            // Map runtime continuation still writes building fields in population
            // order. Synthetic old-format fixtures must start in legacy slot order.
            const auto byGid=[](const auto* a,const auto* b) { return a->gid<b->gid; };
            if (!std::is_sorted(team->liveUnits.begin(),team->liveUnits.end(),byGid)
                || !std::is_sorted(team->liveBuildings.begin(),team->liveBuildings.end(),byGid))
                throw std::logic_error("Synthetic historical fixture requires legacy slot order");
            populationSection=0;
        } else if (teamChild && (name=="myUnits" || name=="myBuildings")) population(name);
    }
    void writeEnterSection(unsigned) override
    { if (!injecting && legacy) sections.emplace_back(); }
    void writeLeaveSection(std::size_t count=1) override
    {
        if (injecting || !legacy) return;
        while (count--) {
            if (sections.empty()) throw std::logic_error("Unbalanced synthetic historical sections");
            if (suppressedAt==sections.size()) suppressedAt=0;
            sections.pop_back();
        }
    }
    void write(const void* data, std::size_t size, const std::string name) override
    { if (!suppressed()) BinaryOutputStream::write(data,size,name); }
    void writeEndianIndependent(const void* data, std::size_t size, const std::string name) override
    { if (!suppressed()) BinaryOutputStream::writeEndianIndependent(data,size,name); }
    void writeUint16(Uint16 value, const std::string name) override
    {
        if (legacy && !injecting && !sections.empty() && sections.back()=="Team" && name=="taskSwapNext") return;
        BinaryOutputStream::writeUint16(value,name);
    }
    void writeUint16Sections(const Uint16* values, std::size_t count, const std::string name) override
    { if (!suppressed()) BinaryOutputStream::writeUint16Sections(values,count,name); }
};
}
