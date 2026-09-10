// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <utility>

// Which resource a building hires its next fetcher for.
//
// A building wants several resources at once, each with its own delivery target,
// and hires one fetcher at a time. Sainte-Lague priority, target / (2 * served +
// 1), sends each successive fetcher to the resource whose served share sits
// furthest below its target share, so the subscriptions track the targets:
// targets of 4 and 4 interleave one for one, targets of 4 and 1 give
// 4-resource, 4-resource, 1-resource, 4-resource, 4-resource.
//
// `served` counts units already subscribed to fetch a resource as well as the
// deliveries that have arrived. Both matter: a subscription is a delivery the
// building has already committed a worker to, and leaving it out lets one
// resource take every slot while the others are staffed by nobody.
namespace FetchApportionment
{
	//! Whether a outranks b under the Sainte-Lague priority above. Cross-multiplied
	//! to stay in integers: the priorities are ratios of small counts and the
	//! simulation must not depend on floating-point rounding.
	inline bool outranks(int targetA, int servedA, int targetB, int servedB)
	{
		return targetA * (2 * servedB + 1) > targetB * (2 * servedA + 1);
	}

	//! Writes the resources with deliveries still outstanding into order, best
	//! first, and returns how many there were. Equal priorities keep the lower
	//! resource index, so the result does not depend on the platform's sort.
	inline int rank(const int *targets, const int *served, int count, int *order)
	{
		int n = 0;
		for (int r = 0; r < count; r++)
			if (targets[r] > served[r])
				order[n++] = r;
		// Insertion sort, stable, so ties fall back to the resource index. n is at
		// most the number of resources a building type wants, in practice one or two.
		for (int i = 1; i < n; i++)
			for (int j = i; j > 0 && outranks(targets[order[j]], served[order[j]],
			                                  targets[order[j - 1]], served[order[j - 1]]); j--)
				std::swap(order[j], order[j - 1]);
		return n;
	}
}
