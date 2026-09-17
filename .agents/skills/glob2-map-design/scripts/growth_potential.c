// SPDX-License-Identifier: GPL-3.0-or-later
// Growth potential of a generated map: how much food its land can regrow, near each colony.
//
// A wheat or wood deposit regrows when a probe at a random offset of up to 15 tiles on each axis
// lands on pure water and the mirrored probe does not land on pure sand (Map::growResources,
// src/map/MapStep.cpp). Each offset's chance is the product of two triangular distributions,
// (16 - |dx|) * (16 - |dy|), so a tile's growth chance is the weighted sum of the water tiles round
// it whose mirror tile is not sand, out of 65,536. This tool computes that exactly for every tile of
// a terrain dump and adds it up over farmland (pure grass and wheat): the sum is "yield", in
// full-fertility tiles. One yield tile holding wheat regrows about 5.4 times per 1,000 ticks (a tile
// is visited about once in 62 ticks, times its chance, times wheat's one-in-three gate).
//
// Build and run (sizes are exponents, 8 = 256; the report gives each colony's swarm corner):
//
//   cc -O2 -o /tmp/growth_potential .agents/skills/glob2-map-design/scripts/growth_potential.c
//   build/src/glob2 --generate-map --generator 54 --map-seed 1 --param teams=4 --param width=8 \
//       --param height=8 --report terrain --output-dir /tmp/dump
//   build/src/glob2 --generate-map karst-towers --seed 1 --width 256 --height 256 --teams 4 \
//       --json /tmp/dump.json --output /tmp/dump.map
//   /tmp/growth_potential /tmp/dump/terrain.txt $(python3 -c "import json; print(' '.join( \
//       '%d %d' % (c['start']['x'], c['start']['y']) for c in json.load(open('/tmp/dump.json'))['map']['colonies']))")
//
// Prints the map's water share, fertile share and total yield, then per colony the yield reachable
// by walking within 24, 48 and 96 steps of its swarm, split into home plots (sealed farmland within
// 20 tiles of the swarm), other sealed plots and open ground. "Sealed" means an eight-connected
// component of farmland smaller than 1,500 tiles, which bunded plots are and open country is not.
// Walking passes grass, sand, beaches, wheat and buildings; water, stone, wood and fruit block it.
// Tile codes are render_terrain.py's: 0 grass, 1 sand, 2 water, 3 mixed, 4 wheat, 5 wood, 6 stone,
// 7 building, 8 fruit, 9 algae.
#include <stdio.h>
#include <stdlib.h>

enum { GRASS = 0, SAND = 1, WATER = 2, MIXED = 3, WHEAT = 4, BUILDING = 7, ALGAE = 9 };
enum { PROBE = 15, SEALED_PLOT = 1500, HOME_REACH = 20, MAX_COLONIES = 64 };

static int width, height;
static int *tiles;

static int wrapIndex(int x, int y)
{
	return ((y % height + height) % height) * width + ((x % width + width) % width);
}
static int isWater(int code) { return code == WATER || code == ALGAE; }
static int isFarmland(int code) { return code == GRASS || code == WHEAT; }
static int isWalkable(int code)
{
	return code == GRASS || code == SAND || code == MIXED || code == WHEAT || code == BUILDING;
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc % 2)
	{
		fprintf(stderr, "usage: %s terrain.txt [swarmX swarmY ...]\n", argv[0]);
		return 2;
	}
	FILE *file = fopen(argv[1], "r");
	if (!file || fscanf(file, "%d %d", &width, &height) != 2)
	{
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 1;
	}
	const int n = width * height;
	tiles = malloc(sizeof(int) * n);
	for (int i = 0; i < n; i++)
		if (fscanf(file, "%d", &tiles[i]) != 1)
		{
			fprintf(stderr, "short terrain dump\n");
			return 1;
		}
	fclose(file);
	const int colonies = (argc - 2) / 2 < MAX_COLONIES ? (argc - 2) / 2 : MAX_COLONIES;

	// Every farmland tile's growth chance, out of 65,536.
	unsigned *chance = calloc(n, sizeof(unsigned));
	int water = 0;
	for (int i = 0; i < n; i++)
		water += isWater(tiles[i]);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
		{
			if (!isFarmland(tiles[y * width + x]))
				continue;
			unsigned sum = 0;
			for (int dy = -PROBE; dy <= PROBE; dy++)
				for (int dx = -PROBE; dx <= PROBE; dx++)
					if (isWater(tiles[wrapIndex(x + dx, y + dy)]) && tiles[wrapIndex(x - dx, y - dy)] != SAND)
						sum += (16 - abs(dy)) * (16 - abs(dx));
			chance[y * width + x] = sum;
		}

	// Farmland components, eight-connected across the wrap, to tell sealed plots from open country.
	int *component = malloc(sizeof(int) * n), *componentSize = calloc(n, sizeof(int));
	int *stack = malloc(sizeof(int) * n);
	for (int i = 0; i < n; i++)
		component[i] = -1;
	int components = 0;
	for (int i = 0; i < n; i++)
	{
		if (component[i] >= 0 || !isFarmland(tiles[i]))
			continue;
		int top = 0, size = 0;
		stack[top++] = i;
		component[i] = components;
		while (top)
		{
			const int p = stack[--top];
			size++;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					const int q = wrapIndex(p % width + dx, p / width + dy);
					if (component[q] < 0 && isFarmland(tiles[q]))
					{
						component[q] = components;
						stack[top++] = q;
					}
				}
		}
		componentSize[components++] = size;
	}

	double totalYield = 0;
	int fertile = 0, atLeast5 = 0, farmland = 0;
	for (int i = 0; i < n; i++)
		if (isFarmland(tiles[i]))
		{
			farmland++;
			totalYield += chance[i] / 65536.0;
			fertile += chance[i] > 0;
			atLeast5 += chance[i] >= 3277;
		}
	printf("map: water %.1f%%, farmland %d tiles, fertile %.1f%% of the map, %d tiles at 5%% or more, "
		   "yield %.0f\n",
		   100.0 * water / n, farmland, 100.0 * fertile / n, atLeast5, totalYield);

	// Per colony: a breadth-first walk from the swarm's 4x4 footprint, then yield by walking range.
	int *steps = malloc(sizeof(int) * n);
	const int ranges[3] = {24, 48, 96};
	for (int c = 0; c < colonies; c++)
	{
		const int sx = atoi(argv[2 + 2 * c]), sy = atoi(argv[3 + 2 * c]);
		for (int i = 0; i < n; i++)
			steps[i] = -1;
		int head = 0, tail = 0;
		for (int dy = 0; dy < 4; dy++)
			for (int dx = 0; dx < 4; dx++)
			{
				const int q = wrapIndex(sx + dx, sy + dy);
				steps[q] = 0;
				stack[tail++] = q;
			}
		while (head < tail)
		{
			const int p = stack[head++];
			if (steps[p] >= ranges[2])
				continue;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					const int q = wrapIndex(p % width + dx, p / width + dy);
					if (steps[q] < 0 && isWalkable(tiles[q]))
					{
						steps[q] = steps[p] + 1;
						stack[tail++] = q;
					}
				}
		}
		printf("colony %d (%d, %d):", c, sx, sy);
		for (int r = 0; r < 3; r++)
		{
			double home = 0, plots = 0, open = 0;
			for (int i = 0; i < n; i++)
			{
				if (steps[i] < 0 || steps[i] > ranges[r] || !isFarmland(tiles[i]) || !chance[i])
					continue;
				int ddx = abs(i % width - sx), ddy = abs(i / width - sy);
				ddx = ddx > width / 2 ? width - ddx : ddx;
				ddy = ddy > height / 2 ? height - ddy : ddy;
				const int sealed = componentSize[component[i]] < SEALED_PLOT;
				const double y = chance[i] / 65536.0;
				if (sealed && ddx <= HOME_REACH && ddy <= HOME_REACH)
					home += y;
				else if (sealed)
					plots += y;
				else
					open += y;
			}
			printf("  <=%d steps: yield %.0f (home %.0f, plots %.0f, open %.0f)", ranges[r],
				   home + plots + open, home, plots, open);
		}
		printf("\n");
	}
	return 0;
}
