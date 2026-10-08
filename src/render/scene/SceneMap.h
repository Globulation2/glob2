// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BitArray.h"
#include "TerrainPresentation.h"
#include "TerrainProperties.h"
#include "CellRules.h"
#include "Ressource.h"
#include "ResourceRegistry.h"
class MapAssetBundle;

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <memory>
#include <vector>
#include <span>

namespace SimulationSnapshot { struct Handle; }
class TerrainRegistry;

//! Read-only queries over retained standard snapshot components. Only the local
//! area masks and material-presence summary are derived presentation storage.
class SceneMap
{
	std::shared_ptr<const TerrainRegistry> registry;
	std::shared_ptr<const ResourceRegistry> resourceDefinitions;
    std::shared_ptr<const MapAssetBundle> assets;
	MaterialMask presentMaterials = 0;
    Uint32 displayedTeamMask = 0;
    bool prepareAreas=true,prepareMaterials=true,derivedComplete=false;
    Uint64 areaRevision=0, resourceRevision=0, configurationRevision=0;
	std::shared_ptr<const SimulationSnapshot::Handle> snapshot;
	const Uint32* snapshotFog = nullptr; // owned by snapshot; copying SceneMap retains it

  public:
    std::string getAreaName(int index) const;

	Uint32 tick = 0;
	SceneMap();
	MaterialMask materialPresence() const { return presentMaterials; }
	Uint16 materialAmountAt(size_t index, unsigned material) const;
	std::shared_ptr<const MapAssetBundle> frozenAssetBundle() const { return assets; }
	const ResourceRegistry& resourceRegistry() const { return *resourceDefinitions; }
	std::shared_ptr<const ResourceRegistry> frozenResourceRegistry() const { return resourceDefinitions; }
	const TerrainRegistry &terrainRegistry() const { return *registry; }
	std::shared_ptr<const TerrainRegistry> frozenTerrainRegistry() const { return registry; }
	const TerrainPresentation &terrainPresentation(TerrainType type) const;
	//! Retain immutable world storage; presentation chunks derive view-specific masks.
	void bindSnapshot(const SimulationSnapshot::Handle& world);
    void bindSnapshot(const SimulationSnapshot::Handle& world, int displayW, int displayH, int localTeam);
    void prepareChunk(size_t first,size_t count);
    void releaseWorld() { snapshot.reset(); snapshotFog=nullptr; scriptAreas={}; }

    bool isFreeForAirUnit(int x,int y) const;
    bool isFreeForGroundUnit(int x,int y,bool canSwim,Uint32 teamMask) const;
    bool isFreeForBuilding(int x,int y,int width,int height) const;
    void displayToMapCaseAligned(int px,int py,int* x,int* y,int vx,int vy) const
    { *x=((px>>5)+vx)&wMask; *y=((py>>5)+vy)&hMask; }
    void cursorToBuildingPos(int mx,int my,int buildingWidth,int buildingHeight,int* px,int* py,int viewportX,int viewportY) const
    {
        *px=(((mx+((buildingWidth&1) ? 0 : 16))>>5)+viewportX)&wMask;
        *py=(((my+((buildingHeight&1) ? 0 : 16))>>5)+viewportY)&hMask;
    }

	bool isPointSet(int n, int x, int y) const
	{
		return !scriptAreas.empty() && (scriptAreas[coordToIndex(x, y)] & (1 << n));
	}
	int getW() const { return w; }
	int getH() const { return h; }
	int getShiftW() const { return wDec; }
    int viewportWidth() const { return displayViewportW; }
    int viewportHeight() const { return displayViewportH; }
	int getMaskW() const { return wMask; }
	int getMaskH() const { return hMask; }
	//! Identity of the source map (Map::identity()); renewed when the map is replaced.
	Uint64 identity() const { return sourceIdentity; }
	//! Stable key for caches of drawn geometry: the same for every extraction of one map.
	Uint64 cacheKey() const { return sourceIdentity; }
	//! Map::terrainSeed() at extraction; salts the terrain material hashes.
	Uint32 terrainSeed() const { return terrainSeedValue; }

	size_t coordToIndex(int x, int y) const { return (size_t(y & hMask) << wDec) + (x & wMask); }
	TerrainType vertexTerrainAt(int x, int y) const;
	//! Corners of cell (x,y): top-left, top-right, bottom-left, bottom-right.
	std::array<TerrainType, 4> cellCorners(int x, int y) const
	{
		return {vertexTerrainAt(x, y), vertexTerrainAt(x + 1, y), vertexTerrainAt(x, y + 1), vertexTerrainAt(x + 1, y + 1)};
	}
	//! The cell's terrain when all four corners agree, otherwise MIXED_TERRAIN.
	TerrainType terrainTypeAt(int x, int y) const;
	const TerrainProperties &terrainPropertiesAt(int x, int y) const;
	// Detailed materials use shipped appearance IDs; custom canonical IDs never
	// index the renderer's fixed builtin binding table.
	TerrainType appearanceAt(int x, int y) const;
	//! The corner terrain a whole-cell view (preview hues, overview) shows.
	TerrainType presentationTypeAt(int x, int y) const;
	const Resource &getResource(int x, int y) const;
	const Resource &getResource(size_t pos) const;
	bool isMapDiscovered(int x, int y, Uint32 visionMask) const;
	bool isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const;
	bool isFOWDiscovered(int x, int y, int visionMask) const;
	//! The fog of war of every tile, indexed like coordToIndex: y * getW() + x, as
	//! the width is a power of two (coordToIndex shifts y by its log2).
	const Uint32 *fogOfWarData() const { return snapshotFog; }
	bool isForbiddenInDisplayedView(int x, int y) const
	{
		return forbiddenView.get(coordToIndex(x, y));
	}
	bool isGuardAreaInDisplayedView(int x, int y) const
	{
		return guardAreaView.get(coordToIndex(x, y));
	}
	bool isClearAreaInDisplayedView(int x, int y) const
	{
		return clearAreaView.get(coordToIndex(x, y));
	}
	bool isFarmAreaInDisplayedView(int x, int y) const
	{
		return farmAreaView.get(coordToIndex(x, y));
	}
	bool canResourcesGrow(int x, int y) const;
    bool canPaintFarmArea(int x, int y) const;
    const Utilities::BitArray& displayedArea(unsigned zone) const
    {
        switch(zone) { case 0:return forbiddenView; case 1:return guardAreaView; case 2:return clearAreaView; default:return farmAreaView; }
    }
	Uint16 getGroundUnit(int x, int y) const;
	Uint16 getAirUnit(int x, int y) const;
	Uint16 getBuilding(int x, int y) const;
	//! Map::isHardSpaceForBuilding: every tile of the rectangle permits buildings, without a
	//! resource or a building.
	bool isHardSpaceForBuilding(int x, int y, int w, int h) const;
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
    void buildingPosToCursor(int x,int y,int width,int height,int* px,int* py,int viewportX,int viewportY) const
    {
        mapCaseToDisplayable(x,y,px,py,viewportX,viewportY);
        *px+=width*16; *py+=height*16;
    }
	void mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY,
									int screenW, int screenH) const;

  private:
	int w = 0, h = 0, wMask = 0, hMask = 0, wDec = 0;
	Uint64 sourceIdentity = 0;
	Uint32 terrainSeedValue = 0;
	int displayViewportW = 0, displayViewportH = 0;
	std::span<const Uint16> scriptAreas;
	Utilities::BitArray forbiddenView, guardAreaView, clearAreaView, farmAreaView;
};
