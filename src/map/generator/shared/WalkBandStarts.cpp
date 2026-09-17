// SPDX-License-Identifier: GPL-3.0-or-later
#include "WalkBandStarts.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Points.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <unordered_map>
namespace MapGeneration
{
WalkBandStarts spreadInWalkBand(const Torus &t, const std::vector<unsigned char> &roomy,
								const std::vector<unsigned char> &walkable,
								const std::vector<int> &toGoal, const std::vector<int> &swimToGoal,
								const Fertility::Field &field, int teams, double mapScale,
								double ringRadius, GenerationContext &context, const std::string &stream,
								std::string_view candidatesKey, const WalkBandRequest &request)
{
	const int n = t.size();
	WalkBandStarts found;
	std::vector<int> distances;
	for (int i = 0; i < n; ++i)
		if (roomy[i])
			distances.push_back(toGoal[i]);
	if (int(distances.size()) < teams)
		return found;
	// Both preferences flood from the site, and farthestSites asks them of the same tiles again and
	// again across its trials and the walks tried, so the answers are kept (a 512 map with eight
	// colonies spent 10 seconds asking).
	std::unordered_map<int, bool> roomyOf;
	// Which roomy tiles are watered, worked out once: the preferences ask it of the same tiles many
	// times over.
	std::vector<unsigned char> wateredSite(n, 0);
	{
		const std::vector<std::uint32_t> means = meanFertilityField(field, t, request.roomRadius);
		for (int i = 0; i < n; ++i)
			wateredSite[i] = roomy[i] && means[i] >= request.fertilityFloor;
	}
	const auto watered = [&](int site) { return wateredSite[site] != 0; };
	const std::function<bool(int)> roomyEnough = [&](int site)
	{
		if (!watered(site))
			return false;
		if (const auto known = roomyOf.find(site); known != roomyOf.end())
			return known->second;
		const Reach reach = reachFrom(t, {site}, walkable, request.catchmentSteps);
		return roomyOf[site] = int(reach.tiles.size()) >= request.catchmentFloor;
	};
	// A site's yield: the crop growth chance summed over the ground its workers reach within
	// yieldSteps, the measure the growth-potential tool uses (a square's mean fertility let one colony
	// start with a 24-step yield of 15 beside neighbours on 36 to 67).
	std::unordered_map<int, std::int64_t> yieldOf;
	const auto yieldAt = [&](int site)
	{
		if (const auto known = yieldOf.find(site); known != yieldOf.end())
			return known->second;
		const Reach reach = reachFrom(t, {site}, walkable, request.yieldSteps);
		std::int64_t yield = 0;
		for (int i : reach.tiles)
			yield += field.at(i % t.w, i / t.w);
		return yieldOf[site] = yield;
	};
	std::int64_t yieldLow = 0, yieldHigh = INT64_MAX;
	const std::function<bool(int)> evenlyFed = [&](int site)
	{
		if (!watered(site))
			return false;
		const std::int64_t yield = yieldAt(site);
		return yield >= yieldLow && yield <= yieldHigh && roomyEnough(site);
	};
	int lastTarget = -1;
	bool spread = false;
	for (const double stretch : {1.0, request.walkStretch})
		for (const int percentile : request.percentiles)
		{
			if (spread)
				break;
			const int greatestWalk = int(
				stretch *
				std::min(int(request.greatestWalkCeiling * (1 + request.walkGrowth * (mapScale - 1))),
						 int(request.greatestWalk * (1 + request.walkGrowth * (mapScale - 1))) +
							 request.walkPerExtraColony * std::max(0, teams - request.extraColoniesFrom)));
			const int target = std::min(greatestWalk, MapGeneration::percentile(distances, percentile));
			if (target == lastTarget)
				continue;
			lastTarget = target;
			const int band = std::max(request.leastBand, target / request.bandDivisor);
			// Searching every walk for the full spacing with twelve colonies took 22 seconds a map on 512.
			const int wantedSpacing =
				std::max(request.leastSpacing,
						 std::min(request.siteSpacing,
								  int(request.ringSpacingShare * 2 * kPi * (target + ringRadius) / teams)));
			std::vector<unsigned char> candidates(n, 0);
			int count = 0;
			for (int i = 0; i < n; ++i)
			{
				candidates[i] = roomy[i] && std::abs(toGoal[i] - target) <= band &&
								swimToGoal[i] * 100 >= target * request.leastSwimPercent;
				count += candidates[i];
			}
			context.telemetry.measure(candidatesKey, count, target);
			if (count < teams)
				continue;
			// The percentiles from an even sample of the candidates, in row order: a flood each is too
			// dear for thousands.
			const int stride = std::max(1, count / request.yieldSamples);
			std::vector<std::int64_t> yields;
			for (int i = 0, seen = 0; i < n; ++i)
				if (candidates[i] && seen++ % stride == 0)
					yields.push_back(yieldAt(i));
			std::sort(yields.begin(), yields.end());
			yieldLow = yields[(yields.size() - 1) * request.yieldLowPercentile / 100];
			yieldHigh = yields[(yields.size() - 1) * request.yieldHighPercentile / 100];
			// The picks prefer evenly fed, roomy sites; where that crowds the colonies together (the
			// accepted ground can lie in one part of the band), roomy sites, then any.
			const struct
			{
				const std::function<bool(int)> *prefer;
				const char *name;
			} preferences[] = {{&evenlyFed, "evenly-fed"}, {&roomyEnough, "roomy"}, {nullptr, "any"}};
			for (const auto &preference : preferences)
			{
				const std::vector<int> sites = farthestSites(t, candidates, walkable, teams, context, stream,
															 request.trials, preference.prefer);
				if (int(sites.size()) < teams)
					continue;
				const int spacing = closestWalk(t, sites, walkable);
				if (spacing > found.spacing)
				{
					found.spacing = spacing;
					found.sites = sites;
					found.distance = target;
					found.band = band;
					found.preference = preference.name;
				}
				if (spacing >= wantedSpacing ||
					(preference.prefer != nullptr &&
					 spacing * 100 >= wantedSpacing * request.goodEnoughSpacingPercent))
				{
					spread = true;
					break;
				}
			}
		}
	return found;
}

std::vector<int> firstWalkTerritories(const Torus &t, const std::vector<unsigned char> &land,
									  const std::vector<int> &sites, int room)
{
	const int n = t.size();
	std::vector<int> territory(n, -1);
	std::vector<int> queue;
	queue.reserve(n);
	for (int k = 0; k < int(sites.size()); ++k)
		for (int dy = -room; dy <= room; ++dy)
			for (int dx = -room; dx <= room; ++dx)
			{
				const int i = t.at(sites[k] % t.w + dx, sites[k] / t.w + dy);
				if (land[i] && territory[i] < 0)
				{
					territory[i] = k;
					queue.push_back(i);
				}
			}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int i = queue[head];
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(i % t.w + dx, i / t.w + dy);
				if (land[j] && territory[j] < 0)
				{
					territory[j] = territory[i];
					queue.push_back(j);
				}
			}
	}
	return territory;
}
} // namespace MapGeneration
