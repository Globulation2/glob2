// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

// Adaptive zoom detail (the adaptiveZoomDetail graphics setting). Uniform scaling
// makes overlays huge when zoomed in and illegible when zoomed out, so each map
// element instead changes representation as a smooth function of how many screen
// points one tile occupies. Every threshold lives here; ramps cross-fade between
// two tile sizes so a wheel notch (1.1x) never pops. Presentation only: the
// simulation never sees it.
struct ZoomDetail
{
	//! One tile's edge in screen points; 32 at 100% on a desktop.
	double tilePoints = 32;
	//! Multiplier from an overlay's legacy size in map pixels to logical screen units.
	double overlayScale = 1;

	//! Opacity of every health, food and occupancy bar.
	float barAll = 1;
	//! Opacity of the bars of entities needing attention; at least barAll.
	float barException = 1;
	//! Opacity of the single status pip that replaces an exception's bars.
	float statusPip = 0;

	//! Zone pattern sprites, the flat tint that replaces them, and the outline.
	float zonePattern = 1, zoneTint = 0, zoneOutline = 1;
	//! Widest zone outline in screen points; 0 leaves it scaling with the map.
	float zoneStrokeMaxPoints = 0;

	//! Flat per-tile colour layer over terrain and resources.
	float terrainOverview = 0;

	float unitSprite = 1, unitMarker = 0;
	//! Worker markers shrink towards the far end so crowds read as density.
	float workerMarkerScale = 1;
	//! Building sprites cross-fade to icon chips in their team's colour.
	float buildingSprite = 1, buildingIcon = 0;
	float flagIcon = 0;
	//! Territory wash and alert rings.
	float strategic = 0;

	// Tile sizes in screen points where each representation starts and finishes
	// its fade. Tune here.
	static constexpr double OverlayPlateauBottom = 20, OverlayPlateauTop = 48;
	static constexpr double OverlayMagnifiedGrowth = 0.6;
	static constexpr double OverlayReducedGrowth = 0.5, OverlaySmallest = 0.6;
	static constexpr double BarAllGone = 16, BarAllFull = 20;
	static constexpr double BarExceptionGone = 9, BarExceptionFull = 12;
	static constexpr double StatusPipGone = 5, StatusPipFull = 8;
	static constexpr double ZonePatternGone = 8, ZonePatternFull = 16;
	static constexpr double ZoneOutlineGone = 5, ZoneOutlineFull = 12;
	static constexpr double ZoneStrokeMaxPoints = 2;
	static constexpr double TerrainOverviewFull = 5, TerrainOverviewGone = 12;
	static constexpr double UnitSpriteGone = 6, UnitSpriteFull = 10;
	static constexpr double WorkerMarkerSmall = 3, WorkerMarkerFull = 6;
	static constexpr double BuildingSpriteGone = 5, BuildingSpriteFull = 8;
	static constexpr double FlagIconFull = 8, FlagIconGone = 12;
	static constexpr double StrategicFull = 4, StrategicGone = 6;

	//! 0 at or below `gone`, 1 at or above `full`, smooth in between.
	static float ramp(double value, double gone, double full)
	{
		const double t = std::clamp((value - gone) / (full - gone), 0.0, 1.0);
		return float(t * t * (3 - 2 * t));
	}

	//! Detail for a map drawn at `zoom`, where one point is `unitsPerPoint`
	//! logical units. Disabled, everything scales with the map as it used to.
	static ZoomDetail forView(double zoom, double unitsPerPoint, bool enabled)
	{
		ZoomDetail detail;
		if (!(unitsPerPoint > 0))
			unitsPerPoint = 1;
		detail.tilePoints = 32 * zoom / unitsPerPoint;
		detail.overlayScale = zoom;
		if (!enabled)
			return detail;
		const double t = detail.tilePoints;
		// Overlays hold their 100% size in points across the plateau. Either side
		// they follow the map far more slowly, so a close-up is of the unit and
		// not its bars, and a bar zoomed out stays legible without burying a crowd.
		double overlay = 1;
		if (t > OverlayPlateauTop)
			overlay = std::pow(t / OverlayPlateauTop, OverlayMagnifiedGrowth);
		else if (t < OverlayPlateauBottom)
			overlay = std::max(OverlaySmallest, std::pow(t / OverlayPlateauBottom, OverlayReducedGrowth));
		detail.overlayScale = unitsPerPoint * overlay;
		detail.barAll = ramp(t, BarAllGone, BarAllFull);
		detail.barException = std::max(detail.barAll, ramp(t, BarExceptionGone, BarExceptionFull));
		detail.statusPip = ramp(t, StatusPipGone, StatusPipFull) * (1 - detail.barException);
		detail.zonePattern = ramp(t, ZonePatternGone, ZonePatternFull);
		detail.zoneTint = 1 - detail.zonePattern;
		detail.zoneOutline = ramp(t, ZoneOutlineGone, ZoneOutlineFull);
		detail.zoneStrokeMaxPoints = float(ZoneStrokeMaxPoints);
		detail.terrainOverview = 1 - ramp(t, TerrainOverviewFull, TerrainOverviewGone);
		detail.unitSprite = ramp(t, UnitSpriteGone, UnitSpriteFull);
		detail.unitMarker = 1 - detail.unitSprite;
		detail.workerMarkerScale = ramp(t, WorkerMarkerSmall, WorkerMarkerFull);
		detail.buildingSprite = ramp(t, BuildingSpriteGone, BuildingSpriteFull);
		detail.buildingIcon = 1 - detail.buildingSprite;
		detail.flagIcon = 1 - ramp(t, FlagIconFull, FlagIconGone);
		detail.strategic = 1 - ramp(t, StrategicFull, StrategicGone);
		return detail;
	}
};
