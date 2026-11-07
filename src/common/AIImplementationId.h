// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The AI implementation identifiers, apart from the AI class so that player and
// header code can name them without including AI.h. AI derives from this struct,
// so AI::ImplementationID and AI::NONE etc. keep working. Saves and replays store
// these values (as P_AI + id), so they never change.
struct AIImplementationIds
{
	enum ImplementationID
	{
		///Reference to AINull
		NONE=0,
		///Reference to AINumbi
		NUMBI=1,
		///Reference to AICastor
		CASTOR=2,
		///Reference to AIWarrush
		WARRUSH=3,
		///Reference to the AISharedRuntime based AIEcono
		ECONO=4,
		///Reference to the AISharedRuntime based AINicowar
		NICOWAR=5,
		///Reference to AICortex (direct AIImplementation binding)
		CORTEX=6,
		///Standalone Maxima strategy AI.
		MAXIMA=7,
		///Reference to AICabino, a resurrected port of the original (2005-2007)
		///Nicowar: a set of independent specialist modules (defense, attack,
		///construction, upgrades, unit/swarm management) that each act on
		///their own but cooperate toward one game plan, direct AIImplementation
		///binding, no AISharedRuntime involved.
		CABINO=8,

		JAVASCRIPT=9,
		SIZE
	};
};
