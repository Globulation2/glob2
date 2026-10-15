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
#include <string_view>
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
    int formatVersion;
    bool injecting = false;
    std::vector<std::string> sections;
    std::size_t suppressedAt = 0;
    int nextTeam = 0;
    Team* team = nullptr;
    unsigned populationSection = 0;

    bool suppressed() const { return suppressedAt && !injecting; }
    void population(std::string_view name)
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
        : BinaryOutputStream(backend), game(game), legacy(version<FILE_FORMAT_VERSION_INTRUSIVE_POPULATION), formatVersion(version) {}
    void writeEnterSection(std::string_view name) override
    {
        if (injecting || !legacy) return;
        const bool teamChild = !sections.empty() && sections.back()=="Team";
        sections.emplace_back(name);
        if (suppressed()) return;
        if (name=="HiringPipeline" && formatVersion<FILE_FORMAT_VERSION_CACHED_HIRING) {
            suppressedAt=sections.size();
            return;
        }
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
    void write(const void* data, std::size_t size, std::string_view name) override
    { if (!suppressed()) BinaryOutputStream::write(data,size,name); }
    void writeEndianIndependent(const void* data, std::size_t size, std::string_view name) override
    { if (!suppressed()) BinaryOutputStream::writeEndianIndependent(data,size,name); }
    void writeUint32(Uint32 value, std::string_view name) override
    {
        if (suppressed()) return;
        BinaryOutputStream::writeUint32(value,name);
        if (formatVersion>=FILE_FORMAT_VERSION_TURRET_TARGETING
            && formatVersion<FILE_FORMAT_VERSION_LEAN_TURRET_TARGETING) {
            if (injecting && populationSection==1 && name=="typeNum")
                BinaryOutputStream::writeUint32(0,"targetingEpoch");
            if (!injecting && !sections.empty() && sections.back()=="turretTargeting" && name=="margin") {
                BinaryOutputStream::writeUint8(1,"forecast");
                BinaryOutputStream::writeUint8(1,"reusePreparation");
            }
            if (!injecting && !sections.empty() && sections.back()=="record" && name=="generation"
                && std::find(sections.begin(),sections.end(),"turretTargeting")!=sections.end()) {
                BinaryOutputStream::writeUint32(0,"epoch");
                for (const char* field : {"x","y","width","height","range","speed","rhythm"})
                    BinaryOutputStream::writeSint32(0,field);
                BinaryOutputStream::writeUint32(0,"unitTeams");
                BinaryOutputStream::writeUint32(0,"buildingTeams");
                BinaryOutputStream::writeSint32(0,"buildingDamage");
                BinaryOutputStream::writeUint32(0,"damageCount");
                BinaryOutputStream::writeUint32(0,"low");
                BinaryOutputStream::writeUint32(0,"high");
                BinaryOutputStream::writeUint32(0,"observed");
            }
        }
    }
    void writeUint16(Uint16 value, std::string_view name) override
    {
        if (legacy && !injecting && !sections.empty() && sections.back()=="Team" && name=="taskSwapNext") return;
        BinaryOutputStream::writeUint16(value,name);
    }
    void writeUint16Sections(const Uint16* values, std::size_t count, std::string_view name) override
    { if (!suppressed()) BinaryOutputStream::writeUint16Sections(values,count,name); }
};
}
