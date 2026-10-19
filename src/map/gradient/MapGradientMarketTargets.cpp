// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "GradientRuntime.h"
#include "GradientMarketTargets.h"
#include "GradientTargetCell.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"

namespace {
void eligibleProviders(const Map& map,int team,int material,std::vector<GradientMarketTargets::Provider>& providers)
{
    providers.clear();
    for(const Building* supplier:map.game->teams[team]->stockSuppliers) {
        if(!map.stockSupplierEligibleSlot(supplier,nullptr,material,1))continue;
        providers.push_back({supplier->scriptIdentity,supplier->gid,supplier->posX,supplier->posY,
            supplier->type->width,supplier->type->height,supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround)});
    }
}
GradientMarketTargets::Footprints supplierFootprints(const Map& map,const std::vector<GradientMarketTargets::Provider>& providers)
{
    GradientMarketTargets::Footprints cells;
    for(const auto& supplier:providers)
        for(int y=0;y<supplier.height;++y)for(int x=0;x<supplier.width;++x) {
            auto& refs=cells[map.coordToIndex(supplier.x+x,supplier.y+y)];
            if(supplier.occupying)++refs.occupying;else ++refs.overlay;
        }
    return cells;
}

}
void Map::ensureGradientMarketTargets(int team,int material)
{
    if(!game || !marketsV2Enabled() || team<0||team>=game->teamsCount()||material<0||material>=int(MaterialCount))return;
    auto& ledger=gradientRuntime->marketTargets.ledgers[GradientMarketTargets::key(team,material)];
    if(ledger.initialized)return;
    eligibleProviders(*this,team,material,ledger.providers);
    ledger.cells=supplierFootprints(*this,ledger.providers);
    for(const auto& [index,refs]:ledger.cells)gradientRuntime->marketTargets.update(index,team,material,refs);
    ledger.initialized=true;
}
void Map::gradientMarketSupplierMasks(std::size_t index,GradientTargetCell& cell) const
{
    const auto found=gradientRuntime->marketTargets.inverse.find(index);
    if(found==gradientRuntime->marketTargets.inverse.end())return;
    for(const auto& [team,masks]:found->second) {
        cell.occupyingSupplies[team]=masks.occupying;cell.overlaySupplies[team]=masks.overlay;
    }
}
bool Map::gradientMarketSupplierGoal(GradientFieldKey key,std::size_t index) const
{
    if(key.family!=GradientFieldFamily::Markets || !game || !key.valid(game->teamsCount(),MOVEMENT_CLASS_COUNT,MaterialCount))return false;
    auto cell=captureGradientTargetCell(index);cell.materials=0;return cell.goal(key);
}
void Map::gradientSupplierTargetsChanged(int team,int material)
{
    if(!game||!marketsV2Enabled()||gradientRuntime->marketTargets.ledgers.empty()||team<0||team>=game->teamsCount()||material<0||material>=int(MaterialCount))return;
    auto& cache=gradientRuntime->marketTargets;
    if(!cache.initialized(team,material))return; // No captured market history exists yet.
    auto& ledger=cache.ledgers.at(GradientMarketTargets::key(team,material));
    eligibleProviders(*this,team,material,ledger.scratch);
    // Amount changes above zero cannot alter membership. Reuse vector capacity
    // and avoid rebuilding sparse footprints unless exact descriptors differ.
    if(ledger.providers==ledger.scratch)return;
    auto next=supplierFootprints(*this,ledger.scratch);
    GradientMarketTargets::diff(ledger.cells,next,[&](std::size_t index,auto /*before*/,auto after) {
        // Snapshot the old provider union, update just this material bit, then
        // let the shared classifier count actual destination membership changes.
        // Overlap (2 suppliers -> 1), natural-source coverage, ground/air rules
        // and occupancy/overlay precedence are handled once by the same rule.
        const auto oldCell=captureGradientTargetCell(index);
        cache.update(index,team,material,after);
        gradientTargetCellChanged(index,oldCell);
    });
    ledger.cells=std::move(next);ledger.providers.swap(ledger.scratch);
}
