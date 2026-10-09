// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneMap.h"
#include "MapAssetBundle.h"

#include "TerrainRegistry.h"
#include "TerrainCornerPresentation.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <algorithm>
#include <bit>

bool SceneMap::isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const
{
	for (int x = x1; x <= x2; x++)
		for (int y = y1; y <= y2; y++)
			if (isMapDiscovered(x, y, visionMask))
				return true;
	return false;
}

// Same conversions as Map's, reading the extracted viewport bounds.
void SceneMap::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX,
									int viewportY) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w - 16) && x * 32 >= displayViewportW)
		x -= w;
	if (y > (h - 16) && y * 32 >= displayViewportH)
		y -= h;
	*px = x << 5;
	*py = y << 5;
}

void SceneMap::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX,
										  int viewportY, int screenW, int screenH) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w / 2 + (screenW / 64)))
		x -= w;
	if (y > (h / 2 + (screenH / 64)))
		y -= h;
	*px = x << 5;
	*py = y << 5;
}

SceneMap::SceneMap() : registry(TerrainRegistry::builtins()), resourceDefinitions(ResourceRegistry::availableDefaults()), assets(MapAssetBundle::empty()) {}

const TerrainPresentation &SceneMap::terrainPresentation(TerrainType type) const
{
	return registry->presentation(type);
}

bool SceneMap::isHardSpaceForBuilding(int x, int y, int w, int h) const
{
	for (int yi = y; yi < y + h; yi++)
		for (int xi = x; xi < x + w; xi++)
		{
			const size_t i = coordToIndex(xi, yi);
			if ((getResource(i).type != NO_RES_TYPE && resourceDefinitions->properties(static_cast<ResourceId>(getResource(i).type)).blocksBuilding) || getBuilding(xi, yi) != 0xFFFF ||
				!terrainPropertiesAt(xi, yi).buildable)
				return false;
		}
	return true;
}

Uint16 SceneMap::materialAmountAt(size_t index,unsigned material) const
{ return snapshot && validMaterial(material) ? MapState::materialAmountAt(snapshot->view(),index,int(material)) : 0; }

void SceneMap::bindSnapshot(const SimulationSnapshot::Handle& world,int displayW,int displayH,int localTeam)
{
    const Uint32 mask=localTeam>=0 && localTeam<32 ? Uint32(1)<<localTeam : 0;
    const bool sameWorld=derivedComplete && sourceIdentity==world.worldIdentity
        && w==world.width && h==world.height;
    prepareAreas=!sameWorld || displayedTeamMask!=mask || areaRevision!=world.mapGenerations[3];
    prepareMaterials=!sameWorld || resourceRevision!=world.mapGenerations[1]
        || configurationRevision!=world.configurationRevision;
    derivedComplete=!prepareAreas && !prepareMaterials;
    const auto previousMaterials=presentMaterials;
    w=world.width;h=world.height;wMask=w-1;hMask=h-1;wDec=std::countr_zero(unsigned(w));
    sourceIdentity=world.worldIdentity;
    terrainSeedValue=world.session->terrainSeed;
    displayViewportW=displayW;displayViewportH=displayH;
    scriptAreas=world.annotations ? std::span<const Uint16>(world.annotations->scriptAreas) : std::span<const Uint16>{};
    const auto count=size_t(w)*h;
    if (forbiddenView.getBitLength()!=count) {
        forbiddenView.resize(count);guardAreaView.resize(count);clearAreaView.resize(count);farmAreaView.resize(count);
    }
    displayedTeamMask=mask;
    areaRevision=world.mapGenerations[3];resourceRevision=world.mapGenerations[1];
    configurationRevision=world.configurationRevision;
    bindSnapshot(world);
    if (!prepareMaterials) presentMaterials=previousMaterials;
}

void SceneMap::bindSnapshot(const SimulationSnapshot::Handle& world)
{
    using namespace SimulationSnapshot;
    auto required = bit(Component::Terrain) | bit(Component::Resources) | bit(Component::Occupancy)
        | bit(Component::Visibility) | bit(Component::Catalogs) | bit(Component::Areas);
    if (world.annotations) required |= bit(Component::Annotations);
    if (world.width != w || world.height != h || world.worldIdentity != sourceIdentity)
        throw std::invalid_argument("PresentationFrame display metadata and snapshot belong to different worlds");
    if (world.growth) required |= bit(Component::Growth) | bit(Component::Rules);
    snapshot = std::make_shared<const Handle>(world.project(required));
    snapshotFog = snapshot->visibility->visible.data();
    tick = world.tick; registry = snapshot->terrain->registry; resourceDefinitions = snapshot->catalogs->resources; assets = snapshot->catalogs->assets;
    presentMaterials = 0;
}

void SceneMap::prepareChunk(size_t first,size_t count)
{
    if (!prepareAreas && !prepareMaterials) return;
    const auto end=std::min(first+count,size_t(w)*h);
    for (size_t i=first;i<end;++i) {
        if (prepareAreas) {
        const auto& area=snapshot->areas->cells[i];
        forbiddenView.set(i,(area.forbidden & displayedTeamMask)!=0);
        guardAreaView.set(i,(area.guard & displayedTeamMask)!=0);
        clearAreaView.set(i,(area.clear & displayedTeamMask)!=0);
        farmAreaView.set(i,(area.farm & displayedTeamMask)!=0);
        }
        if (prepareMaterials) {
        const auto& resource=snapshot->resources->cells[i].resource;
        if (resource.type!=NO_RES_TYPE)
            presentMaterials|=resourceDefinitions->properties(static_cast<ResourceId>(resource.type)).materialMask;
        }
    }
    if (end==size_t(w)*h) derivedComplete=true;
}

TerrainType SceneMap::vertexTerrainAt(int x, int y) const
{ return (*snapshot->terrain->vertices)[coordToIndex(x,y)]; }
TerrainType SceneMap::terrainTypeAt(int x, int y) const
{ const auto c = cellCorners(x, y); return c[0] == c[1] && c[0] == c[2] && c[0] == c[3] ? c[0] : MIXED_TERRAIN; }
const TerrainProperties& SceneMap::terrainPropertiesAt(int x, int y) const
{ return (*snapshot->terrain->rules)[snapshot->terrain->cellRules[coordToIndex(x,y)]].properties; }
TerrainType SceneMap::presentationTypeAt(int x, int y) const { return dominantCornerTerrain(cellCorners(x, y)); }
TerrainType SceneMap::appearanceAt(int x, int y) const { return registry->appearance(presentationTypeAt(x, y)); }
const Resource& SceneMap::getResource(int x, int y) const { return getResource(coordToIndex(x,y)); }
const Resource& SceneMap::getResource(size_t i) const
{ return snapshot->resources->cells[i].resource; }
bool SceneMap::isMapDiscovered(int x, int y, Uint32 mask) const
{ const auto i = coordToIndex(x,y); return ((snapshot->visibility->discovered[i]) & mask) != 0; }
bool SceneMap::isFOWDiscovered(int x, int y, int mask) const
{ return (fogOfWarData()[coordToIndex(x,y)] & mask) != 0; }
bool SceneMap::canResourcesGrow(int x, int y) const
{ const auto i = coordToIndex(x,y); return snapshot->resources->cells[i].mayGrow; }
Uint16 SceneMap::getGroundUnit(int x, int y) const
{ const auto i = coordToIndex(x,y); return snapshot->occupancy->cells[i].groundUnit; }
Uint16 SceneMap::getAirUnit(int x, int y) const
{ const auto i = coordToIndex(x,y); return snapshot->occupancy->cells[i].airUnit; }
Uint16 SceneMap::getBuilding(int x, int y) const
{ const auto i = coordToIndex(x,y); return snapshot->occupancy->cells[i].building; }


bool SceneMap::canPaintFarmArea(int x,int y) const
{ return snapshot && snapshot->canPaintFarmAt(coordToIndex(x,y)); }

bool SceneMap::isFreeForAirUnit(int x,int y) const
{
    const auto& resource=getResource(x,y);
    return terrainPropertiesAt(x,y).flyable && getAirUnit(x,y)==0xffff
        && (resource.type==NO_RES_TYPE || !resourceDefinitions->properties(ResourceId(resource.type)).blocksAir);
}
bool SceneMap::isFreeForGroundUnit(int x,int y,bool canSwim,Uint32 teamMask) const
{
    const auto& terrain=terrainPropertiesAt(x,y);
    const auto& resource=getResource(x,y);
    return (terrain.walkable || (canSwim && terrain.swimmable)) && getGroundUnit(x,y)==0xffff
        && getBuilding(x,y)==0xffff && !(snapshot->areas->cells[coordToIndex(x,y)].forbidden & teamMask)
        && (resource.type==NO_RES_TYPE || !resourceDefinitions->properties(ResourceId(resource.type)).blocksGround);
}
bool SceneMap::isFreeForBuilding(int x,int y,int width,int height) const
{
    if (!isHardSpaceForBuilding(x,y,width,height)) return false;
    for (int dy=0;dy<height;++dy) for (int dx=0;dx<width;++dx)
        if (getGroundUnit(x+dx,y+dy)!=0xffff) return false;
    return true;
}

std::string SceneMap::getAreaName(int index) const
{
    return snapshot && snapshot->annotations && index >= 0 && size_t(index) < snapshot->annotations->areaNames.size()
        ? snapshot->annotations->areaNames[index] : std::string{};
}
