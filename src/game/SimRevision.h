// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The simulation revision. Bump it in every change that can alter what the
// simulation computes from the same MatchSetup and orders, even when the save
// format (VERSION_MINOR) and the network protocol (NET_PROTOCOL_VERSION) stay
// the same: game rules, units, buildings, pathfinding, AI code and parameters,
// order validation, map loading, random number use and scripting. Adding or
// removing a translation catalog needs no bump: SGSL lexes installed catalog codes
// as language tokens, so data/texts.list.txt is one of the hashed sim data files
// and the key moves with it.
//
// It feeds the sim version key (Online::SimVersion, see "Simulation version" in
// docs/multiplayer/turn-protocol.md), which partitions rooms, queues, AI ratings
// and verifiers, so builds that simulate differently never meet in one match.
// A bump also needs a fresh test/fixtures/multiplayer/FourSquares1.g2mr (re-record
// it with run_tests.py --update-fixtures --filter "TurnEngineHarness/the committed*");
// the sim-diff check fails when a pull request simulates differently from its merge
// base without a bump.
//
// Kept apart from Version.h so a bump recompiles only the sim version code.
// deploy/sim_version.py reads this line.
#define SIM_REVISION 144
