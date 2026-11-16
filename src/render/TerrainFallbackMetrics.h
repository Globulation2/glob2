// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

//! How much of one frame's visible terrain came from a fallback rather than the
//! content the view wants, in map cells. MapDrawer::drawMap resets it each
//! frame; benchmarks report it (FRAME_FALLBACK) and time how long after a
//! camera jump or zoom step the view takes to show no fallback at all.
struct TerrainFallbackMetrics
{
	// Detailed terrain pages (the page renderer).
	std::uint64_t pageCells = 0;     //!< Visible cells of the pages the view wants.
	std::uint64_t standInCells = 0;  //!< Of those, drawn from the same page at another density.
	std::uint64_t backdropCells = 0; //!< Of those, with no page at all: the overview shows beneath.
	std::uint64_t fadingCells = 0;   //!< Of those, a page arrived and fading in over a fallback.
	// The overview, drawn at far zoom or as the backdrop of missing pages; only
	// the cells it shows are counted (a backdrop's cells beneath missing pages).
	std::uint64_t overviewCells = 0;      //!< Visible overview cells.
	std::uint64_t overviewOtherCells = 0; //!< Of those, drawn from the other sample density.
	std::uint64_t overviewBaseCells = 0;  //!< Of those, drawn only from the whole-map base.
	std::uint64_t overviewHoleCells = 0;  //!< Of those, with nothing composed: transparent.
	std::uint64_t overviewFadingCells = 0; //!< Of those, a page fading in over the base.

	//! Cells showing anything but the wanted content, settled.
	std::uint64_t fallbackCells() const
	{
		return standInCells + backdropCells + fadingCells + overviewOtherCells + overviewBaseCells +
			overviewHoleCells + overviewFadingCells;
	}
};
