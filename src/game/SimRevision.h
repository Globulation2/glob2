// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The simulation revision. Bump it in every change that can alter what the
// simulation computes from the same MatchSetup and orders, even when the save
// format (VERSION_MINOR) and the network protocol (NET_PROTOCOL_VERSION) stay
// the same: game rules, units, buildings, pathfinding, AI code and parameters,
// order validation, map loading, random number use, scripting.
//
// It feeds the sim version key (Online::SimVersion, see "Simulation version" in
// docs/multiplayer/turn-protocol.md), which partitions rooms, queues, AI ratings
// and verifiers, so builds that simulate differently never meet in one match.
// A bump also needs a fresh test/fixtures/multiplayer/FourSquares1.g2mr (its
// sim version must match); test/check_sim_revision.py and CI fail when the
// committed verification trace or record changes without a bump.
//
// Kept apart from Version.h so a bump recompiles only the sim version code.
// deploy/sim_version.py reads this line.
#define SIM_REVISION 27
