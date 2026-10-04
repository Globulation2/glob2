// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

// Adaptive zoom detail (the adaptiveZoomDetail graphics setting). Uniform scaling
// makes overlays huge when zoomed in and illegible when zoomed out, so each map
// element instead changes representation as a smooth function of how many screen
// points one tile occupies. The map has two looks, the detailed one and the
// strategic overview, and everything that differs between them cross-fades
// inside one narrow window of tile sizes, so each look is undistorted over a
// range of zooms either side. Every threshold lives here. Presentation only:
// the simulation never sees it.
struct ZoomDetail
{
	//! One tile's edge in screen points; 32 at 100% on a desktop.
	double tilePoints = 32;
	//! Multiplier from an overlay's legacy size in map pixels to logical screen units.
	double overlayScale = 1;

	//! Opacity of every health, food and occupancy bar.
	float barAll = 1;
	//! Opacity of the single status pip that stands in for the bars of an
	//! entity needing attention, between the bars fading and the overview.
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
	static constexpr double ZonePatternGone = 9, ZonePatternFull = 16;
	static constexpr double ZoneStrokeMaxPoints = 2;
	//! The cross-fade between the detailed map and the strategic overview:
	//! sprites, terrain, zone outlines and status pips on one side, markers,
	//! icons, flat colours and the territory wash on the other.
	static constexpr double OverviewFull = 7, OverviewGone = 9;
	static constexpr double WorkerMarkerSmall = 3, WorkerMarkerFull = 6;
	//! The tile size a view that cannot zoom out further is drawn as; see rampTile.
	static constexpr double FarEnd = 3.5;
	//! A map that stops zooming out above the overview still gets one: it
	//! holds from fully zoomed out to this many times that tile size, but no
	//! further in than `OverviewReach`. A map whose smallest tile is
	//! `LegibleSmallest` or more never needs it.
	static constexpr double OverviewSpan = 1.5, OverviewSpanLeast = 1.1, OverviewReach = 16;
	static constexpr double LegibleSmallest = 20;
	static constexpr double RemapTop = 20, RemapTopOverFade = 1.25, RemapTopMost = 32;

	//! 0 at or below `gone`, 1 at or above `full`, smooth in between.
	static float ramp(double value, double gone, double full)
	{
		const double t = std::clamp((value - gone) / (full - gone), 0.0, 1.0);
		return float(t * t * (3 - 2 * t));
	}

	//! The tile size the ramps are evaluated at. A small map cannot zoom out far
	//! enough to reach the overview by tile size alone, so when the view's
	//! smallest tile `smallest` is above `FarEnd`, the tile sizes from there
	//! up are remapped: fully zoomed out is always the full overview, it holds
	//! over the first stretch of zooming in, the cross-fade is as narrow as on
	//! any other map, and from normal size in nothing changes. A map so small
	//! that it is legible fully zoomed out is left alone.
	static double rampTile(double tile, double smallest)
	{
		if (!(smallest > FarEnd) || smallest >= LegibleSmallest)
			return tile;
		const double low = std::max({OverviewFull, std::min(OverviewSpan * smallest, OverviewReach),
			OverviewSpanLeast * smallest});
		const double high = low * OverviewGone / OverviewFull;
		const double top = std::clamp(RemapTopOverFade * high, RemapTop, RemapTopMost);
		if (tile >= top)
			return tile;
		if (tile >= high)
			return OverviewGone + (tile - high) * (top - OverviewGone) / (top - high);
		if (tile >= low)
			return OverviewFull + (tile - low) * (OverviewGone - OverviewFull) / (high - low);
		return FarEnd + std::max(0.0, tile - smallest) * (OverviewFull - FarEnd) / (low - smallest);
	}

	//! Detail for a map drawn at `zoom`, where one point is `unitsPerPoint`
	//! logical units. Disabled, everything scales with the map as it used to.
	//! `minimumZoom` is the furthest the view can zoom out, or 0 when unknown.
	static ZoomDetail forView(double zoom, double unitsPerPoint, bool enabled, double minimumZoom = 0)
	{
		ZoomDetail detail;
		if (!(unitsPerPoint > 0))
			unitsPerPoint = 1;
		detail.tilePoints = 32 * zoom / unitsPerPoint;
		detail.overlayScale = zoom;
		if (!enabled)
			return detail;
		// Overlay sizes follow the true tile size; what is drawn follows the
		// tile size measured against how far this map can zoom out.
		const double tile = detail.tilePoints;
		const double t = rampTile(tile, 32 * minimumZoom / unitsPerPoint);
		// Overlays hold their 100% size in points across the plateau. Either side
		// they follow the map far more slowly, so a close-up is of the unit and
		// not its bars, and a bar zoomed out stays legible without burying a crowd.
		double overlay = 1;
		if (tile > OverlayPlateauTop)
			overlay = std::pow(tile / OverlayPlateauTop, OverlayMagnifiedGrowth);
		else if (tile < OverlayPlateauBottom)
			overlay = std::max(OverlaySmallest, std::pow(tile / OverlayPlateauBottom, OverlayReducedGrowth));
		detail.overlayScale = unitsPerPoint * overlay;
		const float detailed = ramp(t, OverviewFull, OverviewGone);
		detail.barAll = ramp(t, BarAllGone, BarAllFull);
		detail.statusPip = detailed * (1 - detail.barAll);
		detail.zonePattern = ramp(t, ZonePatternGone, ZonePatternFull);
		detail.zoneTint = 1 - detail.zonePattern;
		detail.zoneOutline = detailed;
		detail.zoneStrokeMaxPoints = float(ZoneStrokeMaxPoints);
		detail.terrainOverview = 1 - detailed;
		detail.unitSprite = detailed;
		detail.unitMarker = 1 - detailed;
		detail.workerMarkerScale = ramp(t, WorkerMarkerSmall, WorkerMarkerFull);
		detail.buildingSprite = detailed;
		detail.buildingIcon = 1 - detailed;
		detail.flagIcon = 1 - detailed;
		detail.strategic = 1 - detailed;
		return detail;
	}
};
