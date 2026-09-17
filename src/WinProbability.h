/*
  Copyright (C) 2026 Globulation2 contributors

  SPDX-License-Identifier: GPL-3.0-or-later

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __WIN_PROBABILITY_H
#define __WIN_PROBABILITY_H

#include "SDL_net.h"
#include <cstddef>
#include <vector>

class Game;

/// How likely each side is to win, from the state of play.
///
/// The fitted coefficients live in the generated WinProbabilityModel.h; this
/// header is the arithmetic they are evaluated with, and the contract the
/// generator writes against. See docs/win-probability-model.md.
///
/// Everything here is integer arithmetic, deliberately. The optional win
/// probability victory condition asks this code who has won, inside the
/// synchronised simulation, so two machines that disagree by one bit would end
/// the same game on different ticks and desynchronise. Floating point cannot
/// promise that across platforms -- exp() and log() are library-dependent -- so
/// the model was fitted using only transforms that have exact integer forms
/// (identity, a division, a square root) and the softmax is evaluated with a
/// hand-rolled fixed-point exponential. Given the same slots, every platform
/// returns the same permille.
namespace WinProbability
{
	/// Fixed-point scales. Fitness and coefficients carry 32 fractional bits;
	/// fractions and roots use fewer so that one Sint64 multiply per term cannot
	/// overflow, which is what lets this avoid 128-bit arithmetic entirely.
	const int FITNESS_SHIFT = 32;
	const int FRACTION_SHIFT = 20;
	const int ROOT_SHIFT = 10;
	const Sint64 FITNESS_ONE = (Sint64)1 << FITNESS_SHIFT;
	const Sint64 FRACTION_ONE = (Sint64)1 << FRACTION_SHIFT;

	/// The model is read on the 512-tick boundary TeamStats already samples at,
	/// which is the cadence it was fitted on, and never before this tick. The
	/// opening samples can look lopsided for reasons that mean nothing -- one side
	/// with two units and the other with none -- and no model has to defend that.
	/// Kept in step with MINIMUM_DECISION_TICK in tools/win_probability_model.py.
	const int MINIMUM_DECISION_TICK = 5120;

	/// Measurements are clamped here before use. Nothing a team can accumulate
	/// comes close, and the bound is what makes the overflow argument above hold
	/// however strange the state gets.
	const Sint64 COUNT_CLAMP = (Sint64)1 << 24;

	/// One competitor, as the model sees it: a team, or an alliance's totals.
	///
	/// Only values the simulation already maintains appear here. The per-AI
	/// telemetry and the gameplay measurements are both documented as diagnostic
	/// and must never influence play, so neither may be read by a model that
	/// decides a winning condition.
	struct Slot
	{
		Sint32 units;         ///< living units, all types
		Sint32 prestige;      ///< the team's prestige
		Sint32 barracks;      ///< finished barracks
		Sint32 explorers;     ///< living explorers
		Sint32 foodCritical;  ///< units that are starving
		Sint32 attack;        ///< total attack power
		bool alive;           ///< still a competitor; eliminated sides are excluded

		Slot() : units(0), prestige(0), barracks(0), explorers(0),
		         foodCritical(0), attack(0), alive(false) {}
	};

	/// Clamp a raw measurement into the range the arithmetic is proved over.
	Sint64 clampCount(Sint64 value);

	/// value, as an integer count. Multiply a coefficient by this directly.
	Sint64 countValue(Sint64 value);

	/// value / total, with FRACTION_SHIFT fractional bits; 0 when total is 0.
	Sint64 shareValue(Sint64 value, Sint64 total);

	/// sqrt(value), with ROOT_SHIFT fractional bits.
	Sint64 rootValue(Sint64 value);

	/// numerator / denominator, capped at 1, with FRACTION_SHIFT fractional bits.
	Sint64 ratioValue(Sint64 numerator, Sint64 denominator);

	/// exp(-value) for value >= 0, in and out with FITNESS_SHIFT fractional bits.
	///
	/// Deterministic by construction: the exponent is reduced by whole multiples
	/// of ln 2 and the remainder evaluated as a fixed series, using only integer
	/// add, multiply, shift and divide. No libm call, so no platform to disagree
	/// with. Underflows to 0 once the result cannot be represented.
	Sint64 expNegative(Sint64 value);

	/// Integer square root of a non-negative value.
	Sint64 squareRoot(Sint64 value);

	/// Each slot's win probability in permille, summing to 1000 give or take
	/// truncation. Eliminated slots get 0. This is the number the statistics
	/// screen shows and the number the winning condition compares, so what a
	/// player reads is exactly what decided the game.
	std::vector<int> permille(const std::vector<Slot> &slots);

	/// The slots for a game, one per alliance, summed over its teams.
	///
	/// Allies win and lose together, so they are one competitor: that is how the
	/// model was fitted, and splitting them would ask it a question it was never
	/// shown. `allianceOf` receives the same index back for each slot.
	std::vector<Slot> slotsOf(const Game &game, std::vector<int> &allianceOf);

	/// Whether any alliance has reached `thresholdPermille`, and which.
	/// Returns -1 when nobody has.
	int decided(const std::vector<Slot> &slots, int thresholdPermille);
}

#endif
