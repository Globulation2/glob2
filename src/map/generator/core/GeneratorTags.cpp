// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorTags.h"
#include <algorithm>
#include <map>
#include <set>
namespace GeneratorTags
{
namespace
{
// Curated from the glob2-map-design skill's generator-by-generator review
// (.agents/skills/glob2-map-design/references/shaped-generators.md and landscape-generators.md,
// 2026-09-17) rather than guessed from a generator's name: each row reflects what the
// implementation and its design comments actually draw and play like at review time. Re-check
// against the linked generator when its concept changes materially; a tag is a browsing aid, not
// a frozen contract.
const std::map<std::string, std::vector<std::string>> &table()
{
	static const std::map<std::string, std::vector<std::string>> values = {
		{"fingerprint", {"terrain:natural", "feature:river", "feature:maze", "style:tight-building"}},
		{"old-town", {"terrain:urban", "feature:stone-walls", "feature:plazas", "style:tight-building"}},
		{"symmetric-arena", {"terrain:arena", "feature:orchard", "style:contested-center", "fairness:exact-symmetry"}},
		{"swamp", {"terrain:natural", "feature:lakes", "feature:swamp", "style:wide-open"}},
		{"tidal-flats", {"terrain:natural", "feature:islands", "feature:ocean", "style:contested-center", "fairness:repeated-wedge"}},
		{"river", {"terrain:natural", "feature:river", "style:wide-open"}},
		{"isles", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"ring-world", {"terrain:natural", "feature:ocean", "feature:lakes", "style:wide-open"}},
		{"amphitheatre", {"terrain:arena", "feature:stone-walls", "feature:orchard", "style:siege", "style:contested-center"}},
		{"shattered-coast", {"terrain:natural", "feature:ocean", "style:wide-open"}},
		{"crater-lakes", {"terrain:natural", "feature:lakes", "style:wide-open"}},
		{"fjord-continent", {"terrain:natural", "feature:ocean", "feature:islands", "feature:lakes", "style:sprawling"}},
		{"spider-web", {"terrain:arena", "feature:islands", "style:contested-center", "fairness:repeated-wedge"}},
		{"concrete-islands", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"watershed", {"terrain:natural", "feature:river", "style:wide-open"}},
		{"maze", {"terrain:arena", "feature:maze", "feature:stone-walls", "style:tight-building", "style:siege"}},
		{"islands", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"stone-highlands", {"terrain:natural", "feature:stone-walls", "feature:mountains", "style:tight-building"}},
		{"switchbacks", {"terrain:arena", "feature:mountains", "feature:stone-walls", "style:siege", "style:contested-center"}},
		{"city-states", {"terrain:urban", "feature:canals", "feature:islands", "style:tight-building", "style:contested-center"}},
		{"canals", {"terrain:urban", "feature:canals", "style:tight-building"}},
		{"sierpinski-gardens", {"terrain:urban", "feature:canals", "feature:lakes", "style:tight-building", "fairness:exact-symmetry"}},
		{"hilbert-river", {"terrain:urban", "feature:river", "feature:canals", "style:tight-building"}},
		{"lava-shield", {"terrain:natural", "feature:volcanic", "feature:mountains", "style:contested-center"}},
		{"honeycomb-isle", {"terrain:urban", "feature:river", "feature:hexagons", "feature:ruins", "style:tight-building"}},
		{"karst-towers", {"terrain:natural", "feature:river", "feature:mountains", "feature:lakes", "style:tight-building"}},
		{"bajada", {"terrain:natural", "feature:mountains", "feature:desert", "feature:lakes", "style:sprawling"}},
		{"rugged-archipelago", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"contested-commons", {"terrain:natural", "feature:islands", "style:contested-center"}},
		{"rain-shadow", {"terrain:natural", "feature:mountains", "feature:river", "feature:desert", "style:wide-open"}},
		{"everglades", {"terrain:natural", "feature:swamp", "feature:lakes", "style:wide-open"}},
		{"polder", {"terrain:natural", "feature:river", "feature:canals", "style:sprawling"}},
		{"carousel", {"terrain:arena", "feature:stone-walls", "style:siege", "style:tight-building"}},
		{"old-growth", {"terrain:natural", "feature:forest", "style:expansion"}},
		{"anthill", {"terrain:arena", "feature:caves", "feature:stone-walls", "style:tight-building"}},
		{"coral", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"emoji", {"terrain:novelty", "feature:novelty-shapes", "style:tight-building"}},
		{"forts", {"terrain:arena", "feature:stone-walls", "feature:river", "style:siege", "style:fortified"}},
		{"braided-delta", {"terrain:natural", "feature:river", "feature:islands", "style:sprawling"}},
		{"breachable-highlands", {"terrain:arena", "feature:mountains", "feature:stone-walls", "style:siege"}},
		{"hedgerow-country", {"terrain:natural", "feature:forest", "style:tight-building"}},
		{"glacis", {"terrain:arena", "feature:stone-walls", "feature:forest", "feature:river", "style:fortified", "style:siege"}},
		{"allotments", {"terrain:urban", "feature:farmland", "style:tight-building"}},
		{"caravanserai", {"terrain:natural", "feature:desert", "feature:oases", "style:sprawling"}},
		{"braided-river", {"terrain:natural", "feature:river", "feature:islands", "style:sprawling"}},
		{"drumlin-field", {"terrain:natural", "feature:lakes", "style:tight-building"}},
		{"continents", {"terrain:natural", "feature:river", "feature:mountains", "feature:desert", "feature:lakes", "style:wide-open"}},
		{"savannah", {"terrain:natural", "style:wide-open"}},
		{"hills", {"terrain:natural", "feature:terraces", "feature:lakes", "style:tight-building"}},
		{"rice-terraces", {"terrain:natural", "feature:terraces", "feature:river", "style:tight-building"}},
		{"locust", {"terrain:natural", "feature:forest", "style:expansion"}},
		{"plantations", {"terrain:natural", "feature:islands", "feature:ocean", "style:sprawling"}},
		{"uniform", {"special:editor-only"}},
	};
	return values;
}
} // namespace
std::vector<std::string> tagsFor(const std::string &id)
{
	const auto it = table().find(id);
	return it == table().end() ? std::vector<std::string>{} : it->second;
}
std::vector<std::string> categories()
{
	return {"terrain", "feature", "style", "fairness"};
}
std::vector<std::string> valuesFor(const std::string &category)
{
	std::set<std::string> found;
	const std::string prefix = category + ":";
	for (const auto &[id, tags] : table())
		for (const auto &tag : tags)
			if (tag.compare(0, prefix.size(), prefix) == 0)
				found.insert(tag.substr(prefix.size()));
	return {found.begin(), found.end()};
}
} // namespace GeneratorTags
