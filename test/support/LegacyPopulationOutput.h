// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "FileFormatVersions.h"
#include "SaveLoadProfile.h"
#include "gradient/GradientRuntime.h"
#include <BinaryStream.h>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace glob2test
{
// Test-only adapter for synthetic historical files. Header overrides alone do
// not turn current counted population records into the pre-166 slot encoding.
// It also emits the pre-169 empty gradient-job/cache layout for fresh synthetic
// fixtures. Warm material/air caches or admitted jobs require genuine historical
// fixtures instead; silently dropping their state would weaken continuation tests.
// PreservePending explicitly supports representable 164–168 warm-field jobs,
// retaining every private array and deadline while rejecting new cold/cache state.
// The appended probability history is omitted from pre-190 TeamStats records.
// Other historical field differences remain the fixture author's responsibility.
class LegacyPopulationOutput : public GAGCore::BinaryOutputStream
{
    Game* game;
    bool legacy;
    int formatVersion;
    bool preservePending;
    bool injecting = false;
    std::vector<std::string> sections;
    std::size_t suppressedAt = 0;
    int nextTeam = 0;
    Team* team = nullptr;
    unsigned populationSection = 0;

    bool suppressed() const { return suppressedAt && !injecting; }
    bool tracking() const { return formatVersion<FILE_FORMAT_VERSION_WIN_PROBABILITY_HISTORY || formatVersion<FILE_FORMAT_VERSION_MAINLINE_TERRAINS || formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND || legacy || !hasInitialBuildingGradient(formatVersion) || !hasDelayedGradientState(formatVersion); }
    void requireGradientRuntime() const
    {
        if (!game) throw std::logic_error("Synthetic historical runtime requires its owning Game");
        const auto& runtime=*game->map.gradientRuntime;
        if (preservePending) {
            // These formats share the current material/swim slot encoding.
            // Cold demands and new cache families have no historical equivalent.
            if (formatVersion<164
                || !runtime.materialFields.empty()
                || !runtime.fieldRequests.empty())
                throw std::logic_error("Historical pending runtime contains unrepresentable demand or caches");
            const auto legacyEnd=Team::MAX_COUNT*(2*MaterialSlotCount+2)*MOVEMENT_CLASS_COUNT;
            runtime.pipeline.visitLogical([&](auto* slot,unsigned,bool) {
                if (!*slot || game->map.gradientDestination(slot)>=legacyEnd)
                    throw std::logic_error("Historical pending runtime requires a published legacy field destination");
            });
            const auto publishedBuilding=[&](int team,int id,Uint32 identity,int slot) {
                const auto* building=game->teams[team]->myBuildings[id];
                return building && building->scriptIdentity==identity && building->globalGradient[slot];
            };
            if (!hasInitialBuildingGradient(formatVersion)) {
                for (const auto& request:runtime.buildingRequests)
                    if (!request.stale && !publishedBuilding(request.team,request.building,request.identity,request.slot))
                        throw std::logic_error("Historical building request has no published generation");
                runtime.buildings.visitLogical([&](const auto& job,unsigned,bool superseded) {
                    if (!superseded && !publishedBuilding(job.team,job.buildingId,job.scriptIdentity,job.slot))
                        throw std::logic_error("Historical building job has no published generation");
                });
            }
            return;
        }
        if (!runtime.materialFields.empty()
            || runtime.pipeline.pendingCount() || runtime.buildings.pendingCount()
            || !runtime.fieldRequests.empty())
            throw std::logic_error("Synthetic historical fixture requires empty gradient jobs and material/air caches");
    }
    bool observationField(std::string_view name) const
    {
        // Format 166 introduced counted populations, but not owner observation
        // markers. Omit these independently of the older population rewrite so
        // following runtime sections remain aligned in every historical layout.
        return formatVersion < FILE_FORMAT_VERSION_SINGLE_OBSERVATION
            && (name == "worldResultsPublished" || name == "worldResultsPublishedTick"
                || name == "worldWorkSubmitted" || name == "worldWorkSubmittedTick");
    }
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
    enum class GradientLayout { Empty, PreservePending };
    // Historical periodic jobs always refreshed a published plane. Prepare that
    // reference state explicitly instead of serializing a new cold demand as old.
    // Call only in controlled fixtures compiled with access-control diagnostics off.
    static void primeMaterialGradient(Map& map,int team,int material,int swim)
    {
        if (team<0 || team>=Team::MAX_COUNT || material<0 || material>=MaterialCount
            || swim<0 || swim>=SWIM_CLASS_COUNT)
            throw std::invalid_argument("Invalid historical material field key");
        auto& slot=map.materialGradients[team][material][swim];
        bool pending=false;
        for (const auto& request:map.gradientRuntime->fieldRequests) if (request.slot==&slot) pending=true;
        map.gradientRuntime->pipeline.visitLogical([&](auto* destination,unsigned,bool) {
            if (destination==&slot) pending=true;
        });
        if (slot || pending) throw std::logic_error("Historical material reference must precede enrollment");
        auto field=std::make_unique_for_overwrite<Uint16[]>(map.getW()*map.getH());
        map.seedMaterialGradient(team,Uint8(material),swim,field.get());
        map.propagateGradient(field.get(),swim);
        slot=field.release();
        map.publishPlane(&slot);
    }
    LegacyPopulationOutput(GAGCore::MemoryStreamBackend* backend, Game* game, int version,
        GradientLayout gradients=GradientLayout::Empty)
        : BinaryOutputStream(backend), game(game), legacy(version<FILE_FORMAT_VERSION_INTRUSIVE_POPULATION),
          formatVersion(version), preservePending(gradients==GradientLayout::PreservePending) {
        // New unified state cannot be re-labelled as the private174/176–177
        // resource-only policy. Those migrations require genuine saved fixtures.
        if(version<FILE_FORMAT_VERSION_UNIFIED_GRADIENTS
            && hasAdaptiveGradientState(version,game && game->mapHeader.privateAdaptive174))
            throw std::logic_error("Synthetic historical adaptive state requires a genuine saved fixture");
    }
    // Runtime-only historical migration fixtures still retain the owning Game.
    LegacyPopulationOutput(GAGCore::MemoryStreamBackend* backend, Map& map, int version)
        : LegacyPopulationOutput(backend,map.game,version,GradientLayout::PreservePending) {}
    void writeEnterSection(std::string_view name) override
    {
        if (injecting || !tracking()) return;

        const bool teamChild = !sections.empty() && sections.back()=="Team";
        sections.emplace_back(name);
        if (suppressed()) return;
        if (formatVersion<FILE_FORMAT_VERSION_WIN_PROBABILITY_HISTORY && name=="winProbability") {
            suppressedAt=sections.size();
            return;
        }
        if (formatVersion<FILE_FORMAT_VERSION_UNIFIED_GRADIENTS
            && (name=="adaptiveGradients" || name=="adaptiveCapture")) {
            suppressedAt=sections.size();
            return;
        }
        if (formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND
            && (name=="adaptiveResources" || name=="groundUnitsByMovementClass"
                || (name=="capture" && std::find(sections.begin(),sections.end(),"gradientPipeline")!=sections.end()))) {
            suppressedAt=sections.size();
            return;
        }
        if (!hasDelayedGradientState(formatVersion)) {
            if (name=="mapRuntime") requireGradientRuntime();
            if (preservePending && name=="queuedRequests") {
                suppressedAt=sections.size();
                return;
            }
            if (name=="resourceRoutingCache" || name=="airFields"
                || (!preservePending && (name=="gradientPipeline" || name=="buildingGradientPipeline"))) {
                if (name=="gradientPipeline" && formatVersion>=FILE_FORMAT_VERSION_GRADIENT_PIPELINE) {
                    BinaryOutputStream::writeUint8(game->map.gradientRuntime->pipeline.enabled()
                        ? game->map.gradientRuntime->pipeline.delayTicks() : 8,"delay");
                    BinaryOutputStream::writeUint8(0,"count");
                } else if (name=="buildingGradientPipeline" && formatVersion>=FILE_FORMAT_VERSION_BUILDING_GRADIENT_PIPELINE) {
                    BinaryOutputStream::writeUint8(game->map.gradientRuntime->buildings.delayTicks(),"delay");
                    if (hasInitialBuildingGradient(formatVersion)) BinaryOutputStream::writeUint32(0,"count");
                    else BinaryOutputStream::writeUint16(0,"count");
                    BinaryOutputStream::writeUint8(0,"count"); // No admitted results.
                }
                suppressedAt=sections.size();
                return;
            }
        }
        if ((name=="preparedGradients" && formatVersion<FILE_FORMAT_VERSION_HIRING_GRADIENTS)
            || (name=="initializationRequests" && formatVersion<FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS)) {
            suppressedAt=sections.size();
            return;
        }
        if (name=="HiringPipeline" && formatVersion<FILE_FORMAT_VERSION_CACHED_HIRING) {
            suppressedAt=sections.size();
            return;
        }
        if (legacy && name=="Team") {
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
        } else if (legacy && teamChild && (name=="myUnits" || name=="myBuildings")) population(name);
    }
    void writeEnterSection(unsigned index) override
    {
        if(injecting || !tracking()) return;
        const std::string parent=sections.empty()?std::string{}:sections.back();
        const bool mapRuntime=std::find(sections.begin(),sections.end(),"mapRuntime")!=sections.end();
        const bool building=std::find(sections.begin(),sections.end(),"buildings")!=sections.end();
        sections.emplace_back(std::to_string(index));
        if(!suppressed() && mapRuntime && !hasUnifiedMovement(formatVersion)) {
            const unsigned classes=formatVersion>=FILE_FORMAT_VERSION_UNIT_CATALOG?SWIM_CLASS_COUNT:LEGACY_SWIM_CLASS_COUNT;
            const unsigned variants=formatVersion>=FILE_FORMAT_VERSION_UNIT_CATALOG?3:2;
            const bool numeric=!parent.empty() && std::all_of(parent.begin(),parent.end(),[](char c){return c>='0'&&c<='9';});
            if(((parent=="swimClasses" || parent=="gradientUse" || (building && numeric)) && index>=classes)
                || (parent=="access" && index>=variants)) suppressedAt=sections.size();
        }
    }
    void writeLeaveSection(std::size_t count=1) override
    {
        if (injecting || !tracking()) return;

        while (count--) {
            if (sections.empty()) throw std::logic_error("Unbalanced synthetic historical sections");
            const bool writeOldCache=sections.back()=="gradientPipeline"
                && !hasDelayedGradientState(formatVersion)
                && formatVersion>=FILE_FORMAT_VERSION_BUILDING_CATALOG;
            if (suppressedAt==sections.size()) suppressedAt=0;
            sections.pop_back();
            if (writeOldCache) {
                // Before delayed fields, the material cache followed the pipeline.
                // Empty entries have no changed per-entry encoding.
                injecting=true;
                game->map.saveMaterialRoutingCache(this);
                injecting=false;
            }
        }
    }
    void write(const void* data, std::size_t size, std::string_view name) override
    {
        if (suppressed() || observationField(name)
            || (name=="airLocked" && !hasUnifiedMovement(formatVersion))) return;
        BinaryOutputStream::write(data,size,name);
        // Format187 retired this MapHeader word. Relabelled historical fixtures
        // still need the empty requirement list before the resource definitions.
        if (!injecting && name=="SHA1" && !sections.empty() && sections.back()=="MapHeader"
            && formatVersion>=FILE_FORMAT_VERSION_TERRAIN_PROPERTIES
            && formatVersion<FILE_FORMAT_VERSION_MAINLINE_TERRAINS)
            BinaryOutputStream::writeUint32(0,"count");
    }
    void writeEndianIndependent(const void* data, std::size_t size, std::string_view name) override
    { if (!suppressed() && !observationField(name) && !(name=="airLocked" && !hasUnifiedMovement(formatVersion))) BinaryOutputStream::writeEndianIndependent(data,size,name); }
    void writeUint32(Uint32 value, std::string_view name) override
    {
        if (suppressed()) return;
        if (formatVersion<FILE_FORMAT_VERSION_UNIFIED_GRADIENTS
            && (name=="adaptiveKey" || name=="preparedGoalCount")) return;
        if(formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND && name=="count"
            && !sections.empty() && sections.back()=="gradientPipeline") {
            if(value>255) throw std::logic_error("Historical periodic job count exceeds its encoding");
            BinaryOutputStream::writeUint8(Uint8(value),name);
            return;
        }
        if (formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND && name=="movementClassesKnown") return;
        const bool hiring=std::find(sections.begin(),sections.end(),"HiringPipeline")!=sections.end();
        if (!injecting && hiring
            && ((name=="gradients" && formatVersion<FILE_FORMAT_VERSION_HIRING_GRADIENTS)
                || (name=="initializations" && formatVersion<FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS))) {
            if (value) throw std::logic_error("Historical hiring fixture cannot discard prepared or proactive gradients");
            return;
        }
        if (!hasInitialBuildingGradient(formatVersion) && name=="count"
            && sections.size()>=2 && sections.back()=="queue"
            && sections[sections.size()-2]=="buildingGradientPipeline") {
            if (value>65535) throw std::logic_error("Historical building queue exceeds its encoding");
            BinaryOutputStream::writeUint16(Uint16(value),name);
            return;
        }
        if (name=="destination" && !hasUnifiedMovement(formatVersion)) {
            const unsigned classes=formatVersion>=FILE_FORMAT_VERSION_UNIT_CATALOG?SWIM_CLASS_COUNT:LEGACY_SWIM_CLASS_COUNT;
            if (value<5000) { if (value%MOVEMENT_CLASS_COUNT>=classes) throw std::logic_error("Historical air destination"); value=(value/MOVEMENT_CLASS_COUNT)*classes+value%MOVEMENT_CLASS_COUNT; }
            else if (value<10000) { const unsigned key=value-5000; if (key%MOVEMENT_CLASS_COUNT>=classes) throw std::logic_error("Historical air escape destination"); value=5000+(key/MOVEMENT_CLASS_COUNT)*classes+key%MOVEMENT_CLASS_COUNT; }
        }
        if (preservePending && !hasDelayedGradientState(formatVersion) && !injecting) {
            if (!sections.empty() && sections.back()=="gradientPipeline" && name=="requests") {
                if (value) throw std::logic_error("Historical pending runtime cannot encode queued field demand");
                return;
            }
            const bool periodicDestination=name=="destination"
                && std::find(sections.begin(),sections.end(),"gradientPipeline")!=sections.end();
            const bool buildingQueueCount=name=="count" && sections.size()>=2 && sections.back()=="queue"
                && sections[sections.size()-2]=="buildingGradientPipeline";
            if (periodicDestination || (buildingQueueCount && !hasInitialBuildingGradient(formatVersion))) {
                if (value>0xffff) throw std::logic_error("Historical gradient field exceeds its 16-bit encoding");
                BinaryOutputStream::writeUint16(Uint16(value),name);
                return;
            }
        }
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
    void writeUint8(Uint8 value,std::string_view name) override
    {
        if(suppressed()) return;
        if(formatVersion<FILE_FORMAT_VERSION_UNIFIED_GRADIENTS
            && (name=="hasAdaptiveKey" || name=="legacyAssociation")) return;
        if(formatVersion<FILE_FORMAT_VERSION_UNIFIED_GRADIENTS && name=="suspendedStale") {
            // Current writers removed the old visitation/dirty flags. Emit the
            // historical bytes explicitly; a header override cannot restore them.
            // Public historical fixtures predate adaptive stale-state storage.
            if(formatVersion>=FILE_FORMAT_VERSION_GRADIENT_DEMAND)
                BinaryOutputStream::writeUint8(value,name);
            const auto& section=sections.back();
            if(section=="markets") {
                BinaryOutputStream::writeUint8(0,"dirty");
                BinaryOutputStream::writeUint8(0,"updated");
            } else if(section=="guard" || section=="clear"
                || (sections.size()>1 && sections[sections.size()-2]=="resources"))
                BinaryOutputStream::writeUint8(0,"updated");
            return;
        }
        if(formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND && name=="movementClassesKnown") return;
        if(name=="slot" && !hasUnifiedMovement(formatVersion)
            && std::find(sections.begin(),sections.end(),"buildingGradientPipeline")!=sections.end()) {
            const unsigned classes=formatVersion>=FILE_FORMAT_VERSION_UNIT_CATALOG?SWIM_CLASS_COUNT:LEGACY_SWIM_CLASS_COUNT;
            if(value%MOVEMENT_CLASS_COUNT>=classes) throw std::logic_error("Air job cannot be represented in a historical fixture");
            value=(value/MOVEMENT_CLASS_COUNT)*classes+value%MOVEMENT_CLASS_COUNT;
        }
        BinaryOutputStream::writeUint8(value,name);
    }
    void writeUint16(Uint16 value, std::string_view name) override
    {
        if (legacy && !injecting && !sections.empty() && sections.back()=="Team" && name=="taskSwapNext") return;
        if(suppressed()) return;
        if (formatVersion < FILE_FORMAT_VERSION_TEAM_BUILDING_GRADIENT_QUOTA
            && sections.size() >= 2 && sections.back() == "pending"
            && sections[sections.size()-2] == "buildingGradientPipeline" && name == "count") {
            if (value > 255) throw std::logic_error("Historical building pending count exceeds byte encoding");
            BinaryOutputStream::writeUint8(Uint8(value),name);
            return;
        }
        if(formatVersion<FILE_FORMAT_VERSION_GRADIENT_DEMAND && name=="count"
            && !sections.empty() && sections.back()=="gradientPipeline") {
            if(value>255) throw std::logic_error("Historical periodic job count exceeds its encoding");
            BinaryOutputStream::writeUint8(Uint8(value),name);
            return;
        }
        if(name=="destination" && !hasUnifiedMovement(formatVersion)) {
            const unsigned classes=formatVersion>=FILE_FORMAT_VERSION_UNIT_CATALOG?SWIM_CLASS_COUNT:LEGACY_SWIM_CLASS_COUNT;
            if(value%MOVEMENT_CLASS_COUNT>=classes) throw std::logic_error("Air job cannot be represented in a historical fixture");
            value=(value/MOVEMENT_CLASS_COUNT)*classes+value%MOVEMENT_CLASS_COUNT;
        }
        BinaryOutputStream::writeUint16(value,name);
    }
    void writeSint32(Sint32 value, std::string_view name) override
    { if (!suppressed()) BinaryOutputStream::writeSint32(value,name); }

    void writeUint16Sections(const Uint16* values, std::size_t count, std::string_view name) override
    { if (!suppressed()) BinaryOutputStream::writeUint16Sections(values,count,name); }
};
}
