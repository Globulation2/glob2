// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for ZoomDetail, the curves that decide how each map element is
// drawn at a given zoom. The properties of interest: disabling the setting
// reproduces uniform scaling exactly, every curve is monotonic so nothing
// flickers back while zooming one way, and replacements cross-fade rather than
// leaving a zoom range where an element has no representation at all.

#include "Glob2Test.h"

#include "ZoomDetail.h"

#include <initializer_list>

TEST_SUITE("ZoomDetail")
{
	TEST_CASE("Disabled reproduces uniform scaling at every zoom")
	{
		for (double zoom : {0.05, 0.25, 1.0, 3.0, 5.0})
		{
			const ZoomDetail detail = ZoomDetail::forView(zoom, 1, false);
			CHECK_EQ(zoom, detail.overlayScale);
			CHECK_EQ(1.f, detail.barAll);
			CHECK_EQ(1.f, detail.barException);
			CHECK_EQ(0.f, detail.statusPip);
			CHECK_EQ(1.f, detail.zonePattern);
			CHECK_EQ(0.f, detail.zoneTint);
			CHECK_EQ(1.f, detail.zoneOutline);
			CHECK_EQ(0.f, detail.zoneStrokeMaxPoints);
			CHECK_EQ(0.f, detail.terrainOverview);
			CHECK_EQ(1.f, detail.unitSprite);
			CHECK_EQ(0.f, detail.unitMarker);
			CHECK_EQ(1.f, detail.buildingSprite);
			CHECK_EQ(0.f, detail.buildingIcon);
			CHECK_EQ(0.f, detail.flagIcon);
			CHECK_EQ(0.f, detail.strategic);
		}
	}

	TEST_CASE("Normal zoom draws the detailed map with every bar")
	{
		const ZoomDetail detail = ZoomDetail::forView(1, 1, true);
		CHECK_EQ(32.0, detail.tilePoints);
		CHECK_EQ(1.0, detail.overlayScale);
		CHECK_EQ(1.f, detail.barAll);
		CHECK_EQ(1.f, detail.zonePattern);
		CHECK_EQ(1.f, detail.zoneOutline);
		CHECK_EQ(0.f, detail.terrainOverview);
		CHECK_EQ(1.f, detail.unitSprite);
		CHECK_EQ(1.f, detail.buildingSprite);
		CHECK_EQ(0.f, detail.strategic);
	}

	TEST_CASE("Curves are monotonic and replacements cross-fade")
	{
		ZoomDetail previous = ZoomDetail::forView(0.02, 1, true);
		for (double zoom = 0.02; zoom <= 5.0; zoom *= 1.02)
		{
			const ZoomDetail detail = ZoomDetail::forView(zoom, 1, true);
			// Detail only ever appears while zooming in.
			CHECK(detail.barAll >= previous.barAll);
			CHECK(detail.barException >= previous.barException);
			CHECK(detail.zonePattern >= previous.zonePattern);
			CHECK(detail.zoneOutline >= previous.zoneOutline);
			CHECK(detail.unitSprite >= previous.unitSprite);
			CHECK(detail.workerMarkerScale >= previous.workerMarkerScale);
			// Abstractions only ever appear while zooming out.
			CHECK(detail.terrainOverview <= previous.terrainOverview);
			CHECK(detail.flagIcon <= previous.flagIcon);
			CHECK(detail.strategic <= previous.strategic);
			CHECK(detail.buildingSprite >= previous.buildingSprite);
			CHECK_EQ(doctest::Approx(1.0), detail.buildingSprite + detail.buildingIcon);
			// Overlays never shrink while zooming in.
			CHECK(detail.overlayScale >= previous.overlayScale);
			// An element always has a representation.
			CHECK(detail.barException >= detail.barAll);
			CHECK_EQ(doctest::Approx(1.0), detail.zonePattern + detail.zoneTint);
			CHECK_EQ(doctest::Approx(1.0), detail.unitSprite + detail.unitMarker);
			previous = detail;
		}
	}

	TEST_CASE("Overlays hold their size zoomed out and grow slowly zoomed in")
	{
		CHECK_EQ(1.0, ZoomDetail::forView(0.7, 1, true).overlayScale);
		CHECK_EQ(1.0, ZoomDetail::forView(1.5, 1, true).overlayScale);
		const double closeUp = ZoomDetail::forView(5, 1, true).overlayScale;
		CHECK(closeUp > 1.0);
		CHECK(closeUp < 2.5);
		// Zoomed out they shrink, but more slowly than the map and never to nothing.
		const double reduced = ZoomDetail::forView(0.35, 1, true).overlayScale;
		CHECK(reduced < 1.0);
		CHECK(reduced > 0.35);
		CHECK(ZoomDetail::forView(0.05, 1, true).overlayScale >= ZoomDetail::OverlaySmallest);
	}

	TEST_CASE("Thresholds are measured in points, not logical units")
	{
		// A phone with three logical units per point reaches the same detail at
		// three times the zoom, and its overlays are three times as many units.
		const ZoomDetail desktop = ZoomDetail::forView(0.4, 1, true);
		const ZoomDetail phone = ZoomDetail::forView(1.2, 3, true);
		CHECK_EQ(doctest::Approx(desktop.tilePoints), phone.tilePoints);
		CHECK_EQ(doctest::Approx(desktop.barAll), phone.barAll);
		CHECK_EQ(doctest::Approx(desktop.unitSprite), phone.unitSprite);
		CHECK_EQ(doctest::Approx(3 * desktop.overlayScale), phone.overlayScale);
	}

	TEST_CASE("Far zoom is the strategic view")
	{
		const ZoomDetail detail = ZoomDetail::forView(0.06, 1, true);
		CHECK_EQ(0.f, detail.barAll);
		CHECK_EQ(0.f, detail.barException);
		CHECK_EQ(0.f, detail.zonePattern);
		CHECK_EQ(0.f, detail.zoneOutline);
		CHECK_EQ(1.f, detail.terrainOverview);
		CHECK_EQ(1.f, detail.unitMarker);
		CHECK_EQ(1.f, detail.buildingIcon);
		CHECK_EQ(1.f, detail.strategic);
	}
}
