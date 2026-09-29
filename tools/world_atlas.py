#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake real-world geography into the engine's world atlas.

Writes src/map/generator/shared/WorldAtlasData.cpp: one small categorical raster per continent
(ocean, lake, plain, forest, steppe, desert, mountain, tundra, ice, plus a river flag), which the
Continents map generator (and any other generator that wants real geography) fits onto a map. The
generator itself never reads a file: the rasters are compiled into the client, so a map depends on
nothing but the request and the seed, on every platform.

Sources, all downloaded into a cache directory on first use (--cache, default
artifacts/world-atlas/cache, which Git ignores):

  * Natural Earth 1:50m vector data (public domain, naturalearthdata.com, fetched from the
    nvkelso/natural-earth-vector GitHub mirror as GeoJSON): land, lakes, rivers, glaciated areas
    and the named geography regions (deserts, mountain ranges, tundra).
  * The Köppen-Geiger climate classification of Kottek, Grieser, Beck, Rudolf and Rubel (2006),
    "World Map of the Köppen-Geiger climate classification updated", Meteorol. Z. 15, 259-263,
    as the 0.5 degree ASCII grid published at koeppen-geiger.vu-wien.ac.at. It supplies the
    biomes: which land is forest, farmland, steppe, desert, tundra or ice.

Why these: the 1:50m scale resolves a continent at 512 pixels across (about 15 km a pixel) without
hair-thin coastlines, and the 0.5 degree climate grid (about 55 km) is coarse enough to read as
regions of a game map rather than speckle. Natural Earth's named deserts and ranges override the
climate classes so the Sahara, Gobi, Rockies, Andes, Alps and Himalayas are where a player expects.

Everything here is plain Python 3: polygons are filled by an even-odd scanline so holes need no
special handling, and no numpy, GDAL or PIL is required (PIL is used only for --preview PNGs).

    python3 tools/world_atlas.py                     # regenerate the C++ data file
    python3 tools/world_atlas.py --preview artifacts/world-atlas/previews
    python3 tools/world_atlas.py --size 256 --output /tmp/small.cpp

The output is deterministic for a given cache, so re-running it changes nothing unless the
sources or this script change. Bump the Continents generator's revision when the data changes:
its golden rows fingerprint the maps built from it.
"""

import argparse
import datetime
import io
import json
import math
import os
import sys
import urllib.request
import zipfile

NE_BASE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
NE_LAYERS = [
    "ne_50m_land",
    "ne_50m_lakes",
    "ne_50m_rivers_lake_centerlines",
    "ne_50m_geography_regions_polys",
    "ne_50m_glaciated_areas",
]
KG_URL = "http://koeppen-geiger.vu-wien.ac.at/data/Koeppen-Geiger-ASCII.zip"
KG_MEMBER = "Koeppen-Geiger-ASCII.txt"

# The land classes, in the order WorldAtlas.h declares LandClass. Ocean is 0 so an unset cell is
# sea, and the classes a map treats alike are adjacent (the three buildable, farmable kinds first).
CLASSES = ["ocean", "lake", "plain", "forest", "steppe", "desert", "mountain", "tundra", "ice"]
OCEAN, LAKE, PLAIN, FOREST, STEPPE, DESERT, MOUNTAIN, TUNDRA, ICE = range(9)
RIVER = 0x10  # flag bit above the class nibble

# Köppen-Geiger main classes to land classes. The game has three terrains and five deposits, so
# thirty climates collapse to six kinds of land: rainforest and taiga (Af, Am, Dfc-Dwd) are forest,
# where wood dominates; the temperate and continental climates people farm (C*, Dfa/Dfb, Dwa/Dwb,
# Dsa/Dsb) are plain, where wheat dominates; savanna and steppe (Aw, As, BS*) are dry grassland
# with sparse crops; true deserts (BW*) are sand; tundra (ET) is grass nothing grows on; ice (EF)
# is sand too, since nothing lives there.
KOPPEN = {
    "Af": FOREST, "Am": FOREST, "Aw": STEPPE, "As": STEPPE,
    "BWh": DESERT, "BWk": DESERT, "BSh": STEPPE, "BSk": STEPPE,
    "Cfa": PLAIN, "Cfb": PLAIN, "Cfc": PLAIN, "Csa": PLAIN, "Csb": PLAIN, "Csc": PLAIN,
    "Cwa": PLAIN, "Cwb": PLAIN, "Cwc": PLAIN,
    "Dfa": PLAIN, "Dfb": PLAIN, "Dwa": PLAIN, "Dwb": PLAIN, "Dsa": PLAIN, "Dsb": PLAIN,
    "Dfc": FOREST, "Dfd": FOREST, "Dwc": FOREST, "Dwd": FOREST, "Dsc": FOREST,
    "ET": TUNDRA, "EF": ICE,
}
# Natural Earth's tundra polygons are not used: its "Canadian Shield" is boreal forest for the most
# part, and the climate grid already draws the true tundra along the Arctic coasts.
GEOGRAPHY_CLASSES = {"Desert": DESERT, "Range/mtn": MOUNTAIN}
# Only the great rivers: Natural Earth ranks rivers 1 (widest, longest) to 6. At 512 pixels across
# a continent a river is a one-tile line of water on the map, and every one fragments the land, so
# the cut is at rank 3: the Mississippi, Amazon, Nile, Congo, Niger, Danube, Volga, Ob, Yenisei,
# Lena, Ganges, Yangtze, Huang He, Mekong, Murray and their kind, about a hundred worldwide.
RIVER_MAX_RANK = 3

# The Europe/Asia divide, west to east then north: across the Aegean between the Greek islands
# and the Turkish coast, through the Dardanelles and the Sea of Marmara, along Turkey's Black Sea
# coast, over the Caucasus crest (Georgia and Azerbaijan go with Asia), up the Caspian's west
# shore and the Ural river to the Ural mountains and the Kara Sea. Approximate: it is a game map.
EUROPE_ASIA = [
    (28.6, 34.0), (28.6, 35.0), (28.4, 36.4), (27.5, 37.0), (26.6, 38.4), (26.7, 39.0),
    (26.4, 40.2), (29.1, 40.5), (29.2, 41.2), (35.0, 42.0), (41.5, 41.55), (41.5, 43.4),
    (44.0, 43.0), (46.5, 42.2), (48.7, 41.9), (49.0, 41.0), (50.0, 45.5), (52.0, 47.0),
    (57.0, 52.0), (60.0, 56.0), (60.0, 60.0), (62.0, 66.0), (68.0, 69.5), (68.0, 82.0),
]
# The Africa/Asia divide: from the Gaza coast down the Sinai's eastern edge, then the middle of the
# Red Sea and the Gulf of Aden, so the Arabian peninsula goes with Asia and Socotra with Yemen.
AFRICA_ASIA = [
    (34.25, 31.3), (34.9, 29.6), (34.7, 27.0), (37.8, 22.0), (41.0, 16.0), (42.5, 13.0),
    (43.3, 12.55), (43.6, 11.5), (44.0, 12.3), (51.6, 12.3),
]
# The Mediterranean divide between Europe and Africa: Gibraltar to Cap Bon at 36 N (Ceuta and
# Tangier go with Africa, Gibraltar with Europe), then 35 N to the Levant (Sicily, Malta, Crete
# and Cyprus go with Europe, the Libyan and Egyptian coasts lie below 33 N).
EUROPE_AFRICA = [(-6.5, 36.0), (11.7, 36.0), (11.7, 35.0), (34.25, 35.0)]
# Where Asia ends and Oceania begins: New Guinea from the Bird's Head east belongs to Oceania,
# the Moluccas and Timor to Asia, and Australia is everything south of 10.5 S.
ASIA_OCEANIA = [(131.0, 20.0), (131.0, -1.0), (130.5, -1.0), (130.5, -10.5), (100.0, -10.5)]


def box(lon0, lon1, lat0, lat1):
    return [(lon0, lat0), (lon1, lat0), (lon1, lat1), (lon0, lat1)]


# Each continent: its bounding box in degrees, the polygons of neighbouring continents' land to
# cut away, and the map projection. The Americas and Africa are cut from each other at Panama's
# eastern border and the Suez isthmus; Greenland is left out of North America (an ice cap the size
# of Mexico would be a quarter of the map's sand) and so are Hawaii and the Aleutians beyond the
# box. Asia keeps the whole of Indonesia west of New Guinea; Oceania is Australia, New Guinea, New
# Zealand and the Melanesian islands to Fiji.
CONTINENTS = [
    {
        "id": "north-america", "name": "North America",
        "lon": (-168.0, -52.0), "lat": (7.0, 72.0),
        "exclude": [
            box(-75.0, -10.0, 59.0, 90.0),                    # Greenland
            box(-78.0, -30.0, -10.0, 12.5),                   # South America's north coast
            box(-170.0, -150.0, 0.0, 30.0),                   # Hawaii
        ],
    },
    {
        "id": "south-america", "name": "South America",
        "lon": (-82.0, -34.0), "lat": (-56.0, 13.0),
        "exclude": [box(-100.0, -77.5, 8.5, 30.0)],           # Central America
    },
    {
        "id": "africa", "name": "Africa",
        "lon": (-18.0, 52.0), "lat": (-35.0, 38.0),
        "exclude": [
            [(-6.5, 60.0)] + EUROPE_AFRICA + [(34.25, 60.0)],  # Europe, above the divide
            [(34.25, 60.0), (34.25, 31.3)] + AFRICA_ASIA[1:] + [(60.0, 12.3), (60.0, 60.0)],
        ],
    },
    {
        "id": "europe", "name": "Europe",
        "lon": (-25.0, 55.0), "lat": (34.5, 72.0),
        "exclude": [
            [(-6.5, 0.0)] + EUROPE_AFRICA + [(34.25, 0.0)],    # Africa, below the divide
            EUROPE_ASIA + [(200.0, 82.0), (200.0, 34.0)],       # Asia, east of the divide
        ],
    },
    {
        "id": "asia", "name": "Asia",
        "lon": (25.0, 190.0), "lat": (-11.0, 78.0),
        "exclude": [
            EUROPE_ASIA + [(24.5, 82.0), (24.5, 34.0)],         # Europe, west of the divide
            [(24.5, -20.0), (24.5, 32.5)] + AFRICA_ASIA + [(51.6, -20.0)],  # Africa
            [(131.0, -1.5), (200.0, -1.5), (200.0, -60.0), (100.0, -60.0), (100.0, -10.5),
             (131.0, -10.5)],                                   # Oceania
        ],
    },
    {
        "id": "oceania", "name": "Oceania",
        "lon": (110.0, 181.0), "lat": (-48.0, 0.5),
        "exclude": [ASIA_OCEANIA + [(100.0, 20.0)]],           # Indonesia, the Philippines
    },
]


# ---------------------------------------------------------------------------------------------
# Projections. A continent is drawn in an equal-area conic projection (Albers) when it lies well
# away from the equator, which is what an atlas does and what makes North America, Europe and
# Asia look like themselves; a continent straddling or near the equator uses a plain equirectangular
# projection scaled by the cosine of its middle latitude, where a conic would be nearly cylindrical
# anyway and its arithmetic degenerates. Both are exact inverses of each other, which the climate
# lookup needs: every pixel asks what latitude and longitude it is.


class Equirectangular:
    def __init__(self, lon0, lon1, lat0, lat1):
        self.lon0 = lon0
        self.mid = (lon0 + lon1) / 2
        self.k = math.cos(math.radians((lat0 + lat1) / 2))

    def forward(self, lon, lat):
        return (lon - self.mid) * self.k, -lat

    def inverse(self, x, y):
        return x / self.k + self.mid, -y


class Albers:
    """Albers equal-area conic (Snyder 1987, formulas 14-1 to 14-11), standard parallels a sixth
    of the way in from each edge of the latitude range."""

    def __init__(self, lon0, lon1, lat0, lat1):
        self.lam0 = math.radians((lon0 + lon1) / 2)
        phi0 = math.radians((lat0 + lat1) / 2)
        phi1 = math.radians(lat0 + (lat1 - lat0) / 6)
        phi2 = math.radians(lat1 - (lat1 - lat0) / 6)
        self.n = (math.sin(phi1) + math.sin(phi2)) / 2
        self.c = math.cos(phi1) ** 2 + 2 * self.n * math.sin(phi1)
        self.rho0 = math.sqrt(self.c - 2 * self.n * math.sin(phi0)) / self.n

    def forward(self, lon, lat):
        phi = math.radians(lat)
        rho = math.sqrt(max(0.0, self.c - 2 * self.n * math.sin(phi))) / self.n
        theta = self.n * (math.radians(lon) - self.lam0)
        return rho * math.sin(theta), -(self.rho0 - rho * math.cos(theta))

    def inverse(self, x, y):
        y = -y
        rho = math.hypot(x, self.rho0 - y)
        if self.n < 0:
            rho = -rho
            theta = math.atan2(-x, -(self.rho0 - y))
        else:
            theta = math.atan2(x, self.rho0 - y)
        s = (self.c - rho * rho * self.n * self.n) / (2 * self.n)
        phi = math.asin(max(-1.0, min(1.0, s)))
        return math.degrees(self.lam0 + theta / self.n), math.degrees(phi)


def projection_for(spec):
    lon0, lon1 = spec["lon"]
    lat0, lat1 = spec["lat"]
    if abs((lat0 + lat1) / 2) < 25:
        return Equirectangular(lon0, lon1, lat0, lat1)
    return Albers(lon0, lon1, lat0, lat1)


# ---------------------------------------------------------------------------------------------
# Rasterization.


def densify(ring, step=0.5):
    """Points every `step` degrees along a ring's edges, so a straight edge in degrees becomes the
    curve it is under the projection."""
    out = []
    n = len(ring)
    for i in range(n):
        (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % n]
        parts = max(1, int(math.ceil(max(abs(x1 - x0), abs(y1 - y0)) / step)))
        for p in range(parts):
            f = p / parts
            out.append((x0 + (x1 - x0) * f, y0 + (y1 - y0) * f))
    return out


class Canvas:
    """A width by height grid of bytes with a pixel transform from projected units."""

    def __init__(self, width, height, sx, sy, ox, oy):
        self.w, self.h = width, height
        self.sx, self.sy, self.ox, self.oy = sx, sy, ox, oy
        self.cells = bytearray(width * height)

    def to_pixel(self, x, y):
        return (x - self.ox) * self.sx, (y - self.oy) * self.sy

    def from_pixel(self, px, py):
        return px / self.sx + self.ox, py / self.sy + self.oy

    def fill(self, rings, paint):
        """Even-odd scanline fill of pixel-space rings: `paint(index)` for every pixel whose centre
        is inside an odd number of rings. Holes are just inner rings."""
        edges = []
        for ring in rings:
            n = len(ring)
            for i in range(n):
                (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % n]
                if y0 == y1:
                    continue
                if y0 > y1:
                    x0, y0, x1, y1 = x1, y1, x0, y0
                edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0)))
        if not edges:
            return
        edges.sort()
        top = max(0, int(math.floor(min(e[0] for e in edges))))
        bottom = min(self.h - 1, int(math.ceil(max(e[1] for e in edges))))
        active, first = [], 0
        for py in range(top, bottom + 1):
            yc = py + 0.5
            while first < len(edges) and edges[first][0] <= yc:
                active.append(edges[first])
                first += 1
            active = [e for e in active if e[1] > yc]
            xs = sorted(e[2] + (yc - e[0]) * e[3] for e in active if e[0] <= yc)
            row = py * self.w
            for i in range(0, len(xs) - 1, 2):
                xa = max(0, int(math.ceil(xs[i] - 0.5)))
                xb = min(self.w - 1, int(math.floor(xs[i + 1] - 0.5)))
                for px in range(xa, xb + 1):
                    paint(row + px)

    def line(self, points, paint):
        """Bresenham along a polyline of pixel-space points."""
        for (x0, y0), (x1, y1) in zip(points, points[1:]):
            ix0, iy0 = int(math.floor(x0)), int(math.floor(y0))
            ix1, iy1 = int(math.floor(x1)), int(math.floor(y1))
            dx, dy = abs(ix1 - ix0), -abs(iy1 - iy0)
            sx, sy = (1 if ix0 < ix1 else -1), (1 if iy0 < iy1 else -1)
            err = dx + dy
            while True:
                if 0 <= ix0 < self.w and 0 <= iy0 < self.h:
                    paint(iy0 * self.w + ix0)
                if ix0 == ix1 and iy0 == iy1:
                    break
                e2 = 2 * err
                if e2 >= dy:
                    err += dy
                    ix0 += sx
                if e2 <= dx:
                    err += dx
                    iy0 += sy


def geometry_rings(geometry):
    """A GeoJSON Polygon or MultiPolygon as a list of polygons, each a list of rings."""
    if geometry is None:
        return []
    if geometry["type"] == "Polygon":
        return [geometry["coordinates"]]
    if geometry["type"] == "MultiPolygon":
        return geometry["coordinates"]
    return []


def geometry_lines(geometry):
    if geometry is None:
        return []
    if geometry["type"] == "LineString":
        return [geometry["coordinates"]]
    if geometry["type"] == "MultiLineString":
        return geometry["coordinates"]
    return []


class Region:
    """One continent being drawn: its projection, canvas and the longitude wrap it uses."""

    def __init__(self, spec, size):
        self.spec = spec
        self.proj = projection_for(spec)
        self.lon0 = spec["lon"][0]
        # Frame the box: its projected outline's bounds set the scale so the box's longer side is
        # `size` pixels; the continent's own extent is cropped afterwards.
        outline = [self.proj.forward(*p) for p in densify(box(*spec["lon"], *spec["lat"]))]
        xs, ys = [p[0] for p in outline], [p[1] for p in outline]
        span = max(max(xs) - min(xs), max(ys) - min(ys))
        scale = size / span
        width = int(math.ceil((max(xs) - min(xs)) * scale))
        height = int(math.ceil((max(ys) - min(ys)) * scale))
        self.canvas = Canvas(width, height, scale, scale, min(xs), min(ys))
        self.inside = bytearray(width * height)
        self.canvas.fill([self.pixels(box(*spec["lon"], *spec["lat"]))], lambda i: self.inside.__setitem__(i, 1))
        for polygon in spec["exclude"]:
            self.canvas.fill([self.pixels(polygon)], lambda i: self.inside.__setitem__(i, 0))

    def shift(self, points):
        """The longitude shift that carries a ring or line into the box's frame: a whole ring lying
        west of the box's start moves round by 360 degrees, so a box crossing the antimeridian (Asia's,
        which runs on to Alaska) is contiguous. Natural Earth splits every polygon at the antimeridian,
        so a ring never straddles it, and shifting whole rings rather than vertices keeps a ring that
        straddles the box's own start (the Andes at 70 W, for Oceania's box) in one piece."""
        return 360.0 if max(p[0] for p in points) < self.lon0 else 0.0

    def pixels(self, ring):
        shift = self.shift(ring)
        return [self.canvas.to_pixel(*self.proj.forward(p[0] + shift, p[1])) for p in densify(ring)]

    def fill_feature(self, geometry, paint):
        for polygon in geometry_rings(geometry):
            self.canvas.fill([self.pixels(ring) for ring in polygon], paint)

    def draw_feature(self, geometry, paint):
        for line in geometry_lines(geometry):
            dense = []
            for (x0, y0), (x1, y1) in zip(line, line[1:]):
                parts = max(1, int(math.ceil(max(abs(x1 - x0), abs(y1 - y0)) / 0.25)))
                for p in range(parts):
                    f = p / parts
                    dense.append((x0 + (x1 - x0) * f, y0 + (y1 - y0) * f))
            dense.append(tuple(line[-1][:2]))
            shift = self.shift(line)
            self.canvas.line([self.canvas.to_pixel(*self.proj.forward(p[0] + shift, p[1])) for p in dense], paint)

    def lonlat(self, index):
        px, py = index % self.canvas.w + 0.5, index // self.canvas.w + 0.5
        lon, lat = self.proj.inverse(*self.canvas.from_pixel(px, py))
        if lon > 180:
            lon -= 360
        return lon, lat


# ---------------------------------------------------------------------------------------------
# Sources.


def fetch(cache, name, url):
    path = os.path.join(cache, name)
    if not os.path.exists(path):
        os.makedirs(cache, exist_ok=True)
        print("downloading", url, file=sys.stderr)
        with urllib.request.urlopen(url, timeout=120) as response, open(path + ".part", "wb") as out:
            out.write(response.read())
        os.replace(path + ".part", path)
    return path


def load_layers(cache):
    layers = {}
    for layer in NE_LAYERS:
        with open(fetch(cache, layer + ".geojson", NE_BASE + layer + ".geojson"), encoding="utf-8") as f:
            layers[layer] = json.load(f)["features"]
    return layers


class Climate:
    """The 0.5 degree Köppen-Geiger grid: rows of latitude from -89.75 up, columns of longitude
    from -179.75 east; None where the grid has no land."""

    def __init__(self, cache):
        with zipfile.ZipFile(fetch(cache, "Koeppen-Geiger-ASCII.zip", KG_URL)) as z:
            text = z.read(KG_MEMBER).decode("ascii", "replace")
        self.grid = [[None] * 720 for _ in range(360)]
        for line in io.StringIO(text):
            parts = line.split()
            if len(parts) != 3 or parts[2] not in KOPPEN:
                continue
            lat, lon = float(parts[0]), float(parts[1])
            self.grid[int((lat + 90) / 0.5)][int((lon + 180) / 0.5)] = KOPPEN[parts[2]]

    def at(self, lon, lat):
        """The class of the cell containing the point, or of the nearest classified cell within
        three cells (a coast the coarse grid calls sea), or plain when none is that near."""
        row = max(0, min(359, int((lat + 90) / 0.5)))
        col = int((lon + 180) / 0.5) % 720
        for radius in range(0, 4):
            best = None
            for dr in range(-radius, radius + 1):
                for dc in range(-radius, radius + 1):
                    if max(abs(dr), abs(dc)) != radius:
                        continue
                    r = row + dr
                    if 0 <= r < 360:
                        value = self.grid[r][(col + dc) % 720]
                        if value is not None and best is None:
                            best = value
            if best is not None:
                return best
        return PLAIN


# ---------------------------------------------------------------------------------------------
# Drawing a continent.


def draw(spec, layers, climate, size):
    region = Region(spec, size)
    canvas = region.canvas
    cells = canvas.cells
    land = bytearray(canvas.w * canvas.h)
    for feature in layers["ne_50m_land"]:
        region.fill_feature(feature["geometry"], lambda i: land.__setitem__(i, 1))
    for i in range(len(land)):
        if land[i] and region.inside[i]:
            cells[i] = climate.at(*region.lonlat(i))
        else:
            land[i] = 0
    # Named regions override the climate: a desert or range is drawn where the atlas names it.
    for feature in layers["ne_50m_geography_regions_polys"]:
        value = GEOGRAPHY_CLASSES.get(feature["properties"].get("FEATURECLA"))
        if value is None:
            continue

        def paint(i, value=value):
            if land[i]:
                cells[i] = value

        region.fill_feature(feature["geometry"], paint)
    for feature in layers["ne_50m_glaciated_areas"]:
        region.fill_feature(feature["geometry"], lambda i: cells.__setitem__(i, ICE) if land[i] else None)
    for feature in layers["ne_50m_lakes"]:
        region.fill_feature(feature["geometry"], lambda i: cells.__setitem__(i, LAKE) if land[i] else None)
    for feature in layers["ne_50m_rivers_lake_centerlines"]:
        props = feature["properties"]
        if props.get("featurecla") != "River" or (props.get("scalerank") or 9) > RIVER_MAX_RANK:
            continue

        def river(i):
            if land[i] and cells[i] != LAKE:
                cells[i] |= RIVER

        region.draw_feature(feature["geometry"], river)
    # Crop to the land with a one-pixel sea border; the generator adds its own ocean margin.
    xs = [i % canvas.w for i in range(len(land)) if land[i]]
    ys = [i // canvas.w for i in range(len(land)) if land[i]]
    x0, x1 = max(0, min(xs) - 1), min(canvas.w - 1, max(xs) + 1)
    y0, y1 = max(0, min(ys) - 1), min(canvas.h - 1, max(ys) + 1)
    width, height = x1 - x0 + 1, y1 - y0 + 1
    cropped = bytearray(width * height)
    for y in range(height):
        row = (y + y0) * canvas.w + x0
        cropped[y * width:(y + 1) * width] = cells[row:row + width]
    return width, height, cropped


def rle(cells):
    """Byte pairs (value, run), runs of 1 to 255."""
    out = bytearray()
    i = 0
    while i < len(cells):
        run = 1
        while i + run < len(cells) and cells[i + run] == cells[i] and run < 255:
            run += 1
        out += bytes((cells[i], run))
        i += run
    return out


def emit(path, rasters, size):
    stamp = datetime.date.today().isoformat()
    lines = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "// GENERATED by tools/world_atlas.py on %s; do not edit. Regenerate with" % stamp,
        "//   python3 tools/world_atlas.py",
        "// and bump the revision of every generator built on it (their golden rows fingerprint it).",
        "//",
        "// Sources: Natural Earth 1:50m vector data (public domain; naturalearthdata.com) for land,",
        "// lakes, rivers, glaciers and named deserts, ranges and tundra; and the Köppen-Geiger climate",
        "// classification (Kottek et al. 2006, Meteorol. Z. 15, 259-263; koeppen-geiger.vu-wien.ac.at)",
        "// for the biomes. Each region is a %d-pixel-wide categorical raster, run-length encoded as" % size,
        "// (value, run) byte pairs; a value is a WorldAtlas.h LandClass in its low nibble and the river",
        "// flag in bit 4. Regions are cropped to their land plus a one-pixel border of sea.",
        '#include "WorldAtlas.h"',
        "namespace MapGeneration",
        "{",
        "namespace",
        "{",
    ]
    for spec, (width, height, cells) in zip(CONTINENTS, rasters):
        name = "k" + "".join(part.capitalize() for part in spec["id"].split("-")) + "Rle"
        data = rle(cells)
        lines.append("// %s: %d x %d cells, %d bytes encoded." % (spec["name"], width, height, len(data)))
        lines.append("const unsigned char %s[] = {" % name)
        for start in range(0, len(data), 24):
            lines.append("\t" + ",".join(str(b) for b in data[start:start + 24]) + ",")
        lines.append("};")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const AtlasRegion kAtlasRegions[] = {")
    for spec, (width, height, cells) in zip(CONTINENTS, rasters):
        name = "k" + "".join(part.capitalize() for part in spec["id"].split("-")) + "Rle"
        lines.append('\t{"%s", %d, %d, %s, sizeof %s},' % (spec["id"], width, height, name, name))
    lines.append("};")
    lines.append("const int kAtlasRegionCount = %d;" % len(rasters))
    lines.append("} // namespace MapGeneration")
    lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


PALETTE = {
    OCEAN: (30, 60, 120), LAKE: (60, 120, 200), PLAIN: (120, 180, 80), FOREST: (30, 100, 40),
    STEPPE: (190, 190, 110), DESERT: (230, 210, 150), MOUNTAIN: (130, 120, 110),
    TUNDRA: (170, 190, 170), ICE: (240, 245, 250),
}


def preview(directory, spec, width, height, cells):
    try:
        from PIL import Image
    except ImportError:
        print("PIL not installed; no preview for", spec["id"], file=sys.stderr)
        return
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for i, value in enumerate(cells):
        colour = PALETTE[value & 0x0F]
        if value & RIVER:
            colour = (90, 150, 230)
        pixels[i % width, i // width] = colour
    os.makedirs(directory, exist_ok=True)
    image.save(os.path.join(directory, spec["id"] + ".png"))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cache", default=os.path.join(root, "artifacts", "world-atlas", "cache"))
    parser.add_argument("--output", default=os.path.join(root, "src", "map", "generator", "shared", "WorldAtlasData.cpp"))
    parser.add_argument("--size", type=int, default=512, help="pixels across a continent's box (default 512)")
    parser.add_argument("--preview", help="directory for one PNG per continent (needs PIL)")
    parser.add_argument("--only", help="comma-separated continent ids, for a quick look")
    args = parser.parse_args()
    layers = load_layers(args.cache)
    climate = Climate(args.cache)
    rasters = []
    for spec in CONTINENTS:
        if args.only and spec["id"] not in args.only.split(","):
            continue
        print("drawing", spec["name"], file=sys.stderr)
        # Draw once to learn how much of the box the land fills, then again so the land itself, not
        # the box, is `size` pixels on its longer side: every continent is stored at the same detail.
        width, height, cells = draw(spec, layers, climate, args.size)
        width, height, cells = draw(spec, layers, climate, args.size * args.size // max(width, height))
        rasters.append((width, height, cells))
        counts = {}
        for value in cells:
            counts[CLASSES[value & 0x0F]] = counts.get(CLASSES[value & 0x0F], 0) + 1
        print("  %d x %d, encoded %d bytes, %s" % (width, height, len(rle(cells)),
              ", ".join("%s %d%%" % (k, 100 * v // len(cells)) for k, v in sorted(counts.items(), key=lambda kv: -kv[1]))),
              file=sys.stderr)
        if args.preview:
            preview(args.preview, spec, width, height, cells)
    if args.only:
        print("--only given: not writing", args.output, file=sys.stderr)
        return
    emit(args.output, rasters, args.size)
    print("wrote", args.output, file=sys.stderr)


if __name__ == "__main__":
    main()
