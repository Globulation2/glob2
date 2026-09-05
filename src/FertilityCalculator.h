// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#pragma once

#include <functional>

class Map;

namespace FertilityCalculator
{
	/// Reports compute progress in [0, 1]. Invoked from the worker thread.
	using ProgressCallback = std::function<void(float)>;

	/// Computes per-tile fertility, writes it into map.getCase(x,y).fertility,
	/// and updates map.fertilityMaximum. The optional progress callback is
	/// invoked once per column. May be called from a worker thread.
	void compute(Map& map, const ProgressCallback& progress);
}
