// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Layout of the deep diagnostic checksum (Game::checkSum and the per-team, unit,
// building and map words beneath it). Routine network and replay verification use
// the snapshot checksum (SimulationChecksum::snapshotChecksum) instead, which has
// never covered the map header, so the scheme changes only diagnostic traces,
// checksum sidecars and test goldens.
enum class ChecksumScheme : std::uint8_t
{
	// The frozen pre-unit-catalog (format 152) layout that historical goldens use:
	// no catalog, statistics, claim or adaptive-routing words, legacy random
	// stream digests, and a map-header word that mixes in the save format version.
	Legacy152,
	// The live layout. The map-header word covers the team count and required
	// experiments but not the save format version, so a save/reload continuation
	// and a VERSION_MINOR bump leave every word unchanged.
	Current,
};
