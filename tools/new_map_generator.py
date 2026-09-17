#!/usr/bin/env python3
"""Scaffold a new map generator that builds, registers and passes its own validation.

  tools/new_map_generator.py marches "Marches"

Writes src/map/generator/generators/<Name>Generator.{h,cpp} from the designed-generator
shape (design, generate, validateWorld; docs/map-generators/ADDING_A_GENERATOR.md), with
colonies on the roomiest lattice the torus holds (Orbits.h), a round home and pond for
each, the starter kit and the crop guarantee, and three controls to start from. Then:

  * adds its definition to GeneratorRegistry::builtins() and its source to src/SConscript,
  * adds its translation keys to data/texts.keys.txt and, with the English text as a
    placeholder, to every table (translate them before merging),
  * picks the next unused legacy id (or --legacy-id).

What is left is the map itself and the verification the docs ask for:

  scons release=1 -j12 map-generator-golden-test map-generator-defaults-test map-generator-study build/src/glob2
  build/src/MapGeneratorGoldenTest <profile> --update     # records its golden rows
  build/src/glob2 --generate-map <id> --preview artifacts/<id>.png
  See docs/map-generators/CLI.md for comparisons with nearest generators.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATORS = ROOT / 'src' / 'map' / 'generator' / 'generators'
REGISTRY = ROOT / 'src' / 'map' / 'generator' / 'core' / 'GeneratorRegistry.cpp'
SCONSCRIPT = ROOT / 'src' / 'SConscript'
TEXTS = ROOT / 'data'

HEADER = '''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct {Name}Options
{{
	int homeSize;
	int wheat, wood; // percentages of the default amounts
	explicit {Name}Options(const GenerationRequest &r);
}};
GeneratorDefinition {lower}Definition();
'''

SOURCE = '''// SPDX-License-Identifier: GPL-3.0-or-later
#include "{Name}Generator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace MapGeneration;

// {Display}: TODO say what the landscape is, in a sentence a player would recognise.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). TODO: where contact happens and
// when, what remote ground pays, what keeps the map traversable.
//
// Scaffolded by tools/new_map_generator.py: colonies on the roomiest lattice the torus holds, each with
// a round home and a pond. Replace the design with the map's own.
namespace
{{

// A home's pond takes this share of its radius.
constexpr double kPondShare = 0.25;

struct Layout
{{
	Torus t{{1, 1}};
	TerrainSketch terrain;
	std::vector<int> homeOf;       // the colony whose home a tile is, or -1
	std::vector<ShapePoint> homes; // each colony's home centre
	std::vector<ShapePoint> kits;  // where each home's kit goes
	double homeRadius = 0;
	std::string failure;
}};

// The whole layout as a pure function of the request and the context's streams: validateWorld builds it
// again and checks the finished map against it.
Layout design(const GenerationRequest &request, GenerationContext &context)
{{
	const {Name}Options o(request);
	Layout L;
	L.t = {{1 << request.wDec, 1 << request.hDec}};
	const Torus &t = L.t;
	const int teams = request.nbTeams;
	L.terrain.assign(t.size(), GRASS);
	L.homeOf.assign(t.size(), -1);
	L.homeRadius = o.homeSize;
	// Colonies on a lattice (Orbits.h): exact copies of one another when the count has a translation
	// group, evenly spaced rows otherwise.
	const LatticeSites lattice =
		latticeSites(t.w, t.h, teams, context.bounded("{id}-layout", std::uint32_t(t.w)),
					 context.bounded("{id}-layout", std::uint32_t(t.h)));
	for (size_t a = 0; a < lattice.sites.size(); ++a)
		for (size_t b = a + 1; b < lattice.sites.size(); ++b)
		{{
			const double dx = t.offsetX(int(lattice.sites[a].x), int(lattice.sites[b].x));
			const double dy = t.offsetY(int(lattice.sites[a].y), int(lattice.sites[b].y));
			if (dx * dx + dy * dy < (2 * L.homeRadius + 8) * (2 * L.homeRadius + 8))
			{{
				L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
				return L;
			}}
		}}
	if (!homeHasRoom(L.homeRadius))
	{{
		L.failure = "The homes are too small; use a bigger home size.";
		return L;
	}}
	const RadialShape home(L.homeRadius, 0.2, context, "{id}-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "{id}-pond");
	std::vector<unsigned char> water(t.size(), 0);
	for (int k = 0; k < teams; ++k)
	{{
		const ShapePoint centre = lattice.sites[k];
		L.homes.push_back(centre);
		L.kits.push_back(stampRoundHome(t, centre, 0.0, home, L.homeRadius, 1, &pond, water,
										[&](int i) {{ L.homeOf[i] = k; }}));
	}}
	for (int i = 0; i < t.size(); ++i)
		if (water[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	context.telemetry.measure("{id}.home.radius", L.homeRadius);
	context.telemetry.measure("{id}.homes.actual", L.homes.size());
	return L;
}}

bool generate(Game &game, GenerationContext &context)
{{
	context.stage = "{id} layout";
	const {Name}Options o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{{
		context.detail = L.failure;
		return false;
	}}
	Map &map = game.map;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "{id} terrain";
	writeUndermap(map, L.terrain);

	context.stage = "{id} colonies";
	const auto homeMask = [&](int team)
	{{
		std::vector<unsigned char> ground(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	}};
	const auto anchor = [&](int team) {{ return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); }};
	if (!settleColonies(game, context, "{id}-starts", homeMask, anchor))
		return false;

	context.stage = "{id} resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantHomeKit(map, t, context, L.kits[k], 0.0, L.homeRadius, 12, 12, [&](int i)
					 {{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); }});
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {{o.wheat, o.wood, 100, 100, 100}});
	return true;
}}

std::string validateWorld(const Game &game, const GenerationContext &context)
{{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "{display_lower}"); !mismatch.empty())
		return mismatch;
	return walkFromFirstColony(game.map, context.request.nbTeams, "the map", "").error;
}}
}} // namespace

{Name}Options::{Name}Options(const GenerationRequest &r)
	: homeSize(r.option("home-size")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount"))
{{
}}

GeneratorDefinition {lower}Definition()
{{
	return {{"{id}",
			{legacy},
			"{Display}",
			1,
			false,
			{{{{"home-size", "Home size", 10, 24, 1, 14, ControlGroup::Layout}},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount")}},
			generate,
			true,
			nullptr,
			validateWorld,
			// Catalog tags for the landscape picker's filters: several, one "terrain:" and every
			// "feature:" a player would look for, a "style:", and a "fairness:" when the homes are
			// fair by construction. Placeholders: replace them with the map's own.
			{{"terrain:natural", "feature:lakes", "style:wide-open", "fairness:stamped-lattice"}}}};
}}
'''


def camel(identifier):
    return ''.join(part.capitalize() for part in identifier.split('-'))


# Legacy ids of generators that were dropped; docs/map-generators/MAP_GENERATOR_FRAMEWORK.md keeps the
# list. They stay taken: a saved game or a replay that names one must not resolve to a newer map.
RETIRED_LEGACY_IDS = {25, 33}


def used_legacy_ids():
    ids = set(RETIRED_LEGACY_IDS)
    for path in GENERATORS.glob('*.cpp'):
        for match in re.finditer(r'return\s*\{\s*"[a-z0-9-]+",\s*(\d+),', path.read_text()):
            ids.add(int(match.group(1)))
    return ids


def append_keys(keys):
    listing = (TEXTS / 'texts.list.txt').read_text().split()
    key_file = ROOT / listing[0]
    existing = set(key_file.read_text().split('\n'))
    fresh = [k for k in keys if f'[{k}]' not in existing]
    if not fresh:
        return
    with key_file.open('a') as f:
        for k in fresh:
            f.write(f'[{k}]\n')
    for name in listing[1:]:
        table = ROOT / name
        text = table.read_text()
        if text and not text.endswith('\n'):
            text += '\n'
        text += ''.join(f'[{k}]\n{k}\n' for k in fresh)
        table.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('id', help='stable string id, lower-case words joined by hyphens')
    parser.add_argument('display', help='display name, which is also its translation key')
    parser.add_argument('--legacy-id', type=int, help='numeric id (default: the next unused)')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9]*(-[a-z0-9]+)*', args.id):
        sys.exit('the id is lower-case words joined by hyphens, like "tidal-flats"')
    name = camel(args.id)
    header, source = GENERATORS / f'{name}Generator.h', GENERATORS / f'{name}Generator.cpp'
    if header.exists() or source.exists():
        sys.exit(f'{source.name} already exists')
    used = used_legacy_ids()
    legacy = args.legacy_id if args.legacy_id is not None else max(used) + 1
    if legacy in used:
        sys.exit(f'legacy id {legacy} is taken; ids are never reused')
    lower = name[0].lower() + name[1:]
    fields = dict(Name=name, lower=lower, id=args.id, Display=args.display,
                  display_lower=args.display.lower(), legacy=legacy)
    header.write_text(HEADER.format(**fields))
    source.write_text(SOURCE.format(**fields))

    registry = REGISTRY.read_text()
    last_include = list(re.finditer(r'#include "[A-Za-z]+Generator\.h"\n', registry))[-1]
    registry = registry[:last_include.end()] + f'#include "{name}Generator.h"\n' + registry[last_include.end():]
    # New generators join the designed ones, ahead of the legacy families.
    marker = 'ruggedArchipelagoDefinition()'
    if marker not in registry:
        sys.exit('GeneratorRegistry.cpp no longer lists ruggedArchipelagoDefinition(); register by hand')
    registry = registry.replace(marker, f'{lower}Definition(), {marker}', 1)
    REGISTRY.write_text(registry)

    sconscript = SCONSCRIPT.read_text()
    anchor = 'map/generator/generators/WatershedGenerator.cpp\n'
    if anchor not in sconscript:
        sys.exit('src/SConscript no longer lists WatershedGenerator.cpp; add the source by hand')
    SCONSCRIPT.write_text(sconscript.replace(anchor, anchor + f'map/generator/generators/{name}Generator.cpp\n', 1))

    append_keys([args.display, 'Home size', 'Too many colonies for this map; use a bigger map or fewer colonies.',
                 'The homes are too small; use a bigger home size.'])
    print(f'Created {header.relative_to(ROOT)} and {source.relative_to(ROOT)} (legacy id {legacy}).')
    print('Registered it, added its source to src/SConscript and its keys to the translation tables')
    print('(English placeholders: translate them). Next:')
    print('  scons release=1 -j12 map-generator-golden-test map-generator-defaults-test map-generator-study build/src/glob2')
    print('  build/src/MapGeneratorGoldenTest <profile> --update')
    print(f'  build/src/glob2 --generate-map {args.id} --preview artifacts/{args.id}.png')
    print('  Compare nearest generators at 128, 256, 512: docs/map-generators/CLI.md')


if __name__ == '__main__':
    main()
