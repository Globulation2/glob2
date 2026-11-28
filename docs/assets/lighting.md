# Lighting and shadow direction

Everything lit or shadowed in the world shares one sun, so that units,
buildings, resources, terrain, clouds and effects read as standing in the same
light. New artwork, generators and renderers follow it; this page records the
direction, where each part of the game encodes it, and the known exceptions.

## The canonical sun

Directions are in screen space: x to the right, y **down**, and z toward the
viewer (up out of the ground in the top-down view). Angles are measured
counter-clockwise from screen-right, so 90° is the top of the screen and 135°
the upper left.

| Quantity | Value |
| --- | --- |
| Light comes from | the **upper left**, horizontal direction (−0.6, −0.8), about 127° |
| Sun elevation for shading | about 42° above the ground; as a 3D vector toward the light, normalize(−0.62, −0.78, 0.9). Lit faces and highlights use it; cast ground shadows are shorter, see [ground shadows](#ground-shadows) |
| Shadows fall toward | the **lower right**, direction (0.6, 0.8), about 53° below horizontal |
| Lit faces | those turned up and to the left; rims and edges facing the lower right fall dark |
| Tolerance | new work stays within ±10° of 127° (shadows within ±10° of 53° below horizontal) |

A highlight, rim light or bevel is lit on the side facing the light; a cast
shadow is the caster moved along (0.6, 0.8); edge darkening sits on the side
away from the light. Effects with no direction (glows, radial rims, centred
contact shadows, fog) are neutral and fine anywhere.

## Where it is encoded

| Area | Constant | Notes |
| --- | --- | --- |
| Runtime (C++) | `src/render/Sunlight.h` | Cloud shadows, flying units' shadows, ground shadow opacity and the torus specular sun. Use it for any new runtime shading. |
| Building and resource shadows | `tools/artwork/cast_shadows.py` reads `shadowX`, `shadowY` and `flightHeight` from `Sunlight.h` | See [ground shadows](#ground-shadows). |
| Flying units | `src/render/GameRenderUnits.cpp` | Explorer renders carry no ground shadow; the air-unit pass first draws each flyer's silhouette `flightHeight` along the sun, over ground units and buildings, then every flyer above them. The throw is a little longer than the swarm's tallest shadow, so explorers read as flying above it. |
| In-game cloud shadows | `src/render/clouds/DynamicClouds.cpp` | The shadow is the cloud field moved along the sun by `cloudShadowThrow` × `cloudHeight`. |
| Intro cloud cover | `src/render/clouds/IntroCover.cpp` | Shadow offset (30, 40) and puff slope (7, 9), shared with the GL shader. |
| Unit skins | `libs/gag/shaders/skin-material.glsl` | `(-0.4, 0.7, 1.0)` with y **up**, about 120°: matches the classic unit renders. |
| Torus view | `src/render/torus/TorusViewRender.cpp`, `TorusViewDevice.cpp` | Diffuse `(-0.35, -0.45, 0.82)`; specular from `Sunlight::glancing*`. |
| Terrain decor | `tools/artwork/terrain_decor.py` `LIGHT`, `SHADOW`, `LONG` | Contact and long shadows both fall along `SHADOW`, straight away from `LIGHT`. |
| Terrain masses and outcrops | `terrain_masses.py`, `terrain_outcrops.py` `LIGHT` | Same azimuth, separate constants. |
| Ground materials | `terrain_core.shade` / `gradient_light` default `(-0.7, -0.7)` | Recipes may override within the tolerance. |
| Painted resources | `paint_landscape.py`, `paint_resources.py` `LIGHT` | `(-0.55, -0.75, 0.9)` and a (1.6, 1.3) shadow shift. |
| Unit-effect art | `src/render/UnitEffectArt.cpp` | Heart shading and icon shadows. |
| AI art prompts | `services/online/apps/ai-*-worker/src/artwork.ts` | Ask for upper-left lighting. |
| Game UI chrome | panel and card shadows offset (2, 3) | Matches the sun, though UI need not. |

When a field is shaded from its gradient, remember the sign: a raised shape's
height rises inward from its rim, so the face lit by a light vector `L` is
where `-∇h · L` is positive, and a normal-based Lambert term is `n · L` with `n`
pointing out of the surface. Check new shading on a synthetic disc: the
upper-left rim must come out brighter than the lower-right one.

## Ground shadows

Buildings and resources do not bake their shadows into their sprites. Each
frame that casts one has a separate **shadow layer** `<frame>s` (classic
`content/data/gfx/<frame>s.png` and HD `<frame>s.png`, 4× the classic one):
black, its alpha the shadow's coverage at full strength, sharing the frame's
top-left corner and larger by whatever reaches past it to the lower right.
The game draws them in one pass after the terrain and before resources, units
and buildings (`src/render/ShadowMask.h`): every shadow reaching a map chunk is
merged with a maximum, so overlapping shadows never stack, and the merged mask
is drawn at `Sunlight::groundShadowOpacity` (0.32). Nothing standing in a
shadow is darkened by it, and a building drawn later covers the shadows behind
it. See [rendering](../architecture/rendering.md#ground-shadow-pass).

`tools/artwork/cast_shadows.py` registers every layer, following
`content-src/gfx/shadows/specs.json`, deterministically and offline:

| Method | Used for | How |
| --- | --- | --- |
| `mesh` | buildings and their construction frames | the mask `tools/artwork/mesh_shadows.py` cast from a TRELLIS 3D model of the sprite (`content-src/gfx/shadows/mesh/<frame>s.png`, with its record `<frame>.json`) |
| `block` with a height | building sites, wheat and rocks (8–10 HD px contact shadows), and buildings whose meshes fail the gate | the footprint is the silhouette (base alpha ≥ 200 or team alpha ≥ 20) eroded upward by the height, swept `ratio` × the height along the sun |
| `block` with `blocks` and `spires` | fallback geometry kept on every building entry | each box of the silhouette gets its own height; a spire pixel's height is its row above `base_y` plus `base_h` |
| `canopy` | trees and fruit trees | each canopy pixel lands 0.5 × its height above the trunk along the sun, blurred 7 HD px, plus a trunk contact shadow |

The swarm keeps the shadow drawn into its art and has no spec.

**Building meshes.** `tools/artwork/ai/trellis_meshes.py` sends each building
sprite (de-shadowed, team layer composited, 2×) to the TRELLIS image-to-3D
model (MIT weights, hosted by fal.ai) and stages the meshes outside the
repository; they are not committed. `mesh_shadows.py` aligns each mesh to the
sprite with an orthographic camera at the sprites' one shared view, 59.6° below
the horizontal (yaw over the full turn, mirrored or not; roll, scale and
translation refined), gates it (tolerant silhouette fit ≥ 0.80, a solid
footprint rather than a relief, no taller than the sprite; up to four seeds,
the best passing one kept, preferring the height closest to the building's
other variants; variants with the same silhouette share one mask), and casts it: a visible surface point H screen px
above the mesh's ground stands at (x, y + H) and throws `length` × H, at most
45 native px, along (0.6, 0.8), blurred 2 HD px. A world height h shows as
h × cos 59.6° on screen. A point's ground stays on its own piece of the sprite,
no lower than that piece's base in its column, so a mesh seated too low casts no
slab below the building and separate pieces cast separately; shadow parts not
touching the sprite are dropped. A spec can override this per building: `mesh` names another frame whose mask this one borrows, aligned at the
bottom-right corner as buildings are drawn and clipped to what this frame's
silhouette can throw along the sun; `mesh_scale` scales the mask about the
building's base, and `mesh_smooth` opens it with a disc of that radius to
remove thin parts poking out (hospital level 1 borrows level 3's shape at
0.65, smoothed by 14 HD px). Buildings
whose meshes fail the gate keep a `block` spec at their reviewed length. Where
TRELLIS models a building badly, a `proxy` spec describes a stand-in by hand
in the sprite's HD pixels (defence tower level 2: a tapered cylinder, `frustum`,
from its rim and ground ellipses, and three `banners`, each a box of team-layer
pixels with the ground row they stand on), and `cast_shadows.py` throws every
point from its true height, so the banners held up on the rim cast apart from
the base. barracks0b0's old shadow was painted
opaque into its art; two masked FLUX.1 Fill [pro] passes (fal.ai) repainted it
as sand before the mesh was made, recorded with their masks, prompts and hashes
in `content-src/gfx/shadows/inpaint/` (the classic base takes the HD result
area-averaged 4×). The pass outputs keep the input alpha; `cast_shadows.py`'s
`strip` then clears the translucent near-black edge pixels as for every frame.
A mesh record's `sprite` hashes name the base the mesh was aligned to, before
that strip; rerun `mesh_shadows.py` whenever a frame's opaque art changes.

**Shadow length is art direction.** One sun direction serves everything, but
each building's `length` (screen px of throw per screen px of height, default
`SHADOW_LENGTH` 0.6) is set in specs.json so small buildings read as low and
towers as tall (from 0.2 for pools and the barracks' sunken arena to 1.1 for
the level 3 hospital). Reviewers compare a building at every candidate length
with `mesh_shadows.py --lengths DIR`; where two reviewers differed, the shorter
length was taken. In ground terms, with the camera at 59.6°, a
screen length r is a ground shadow of 0.56 r per unit of height
(r cos 59.6° √(0.6² + 0.8²/sin² 59.6°)), so the sun stands at
atan(1 / 0.56 r):

| length | 0.2 | 0.3 | 0.45 | 0.6 | 0.8 | 1.1 |
| --- | --- | --- | --- | --- | --- | --- |
| true sun elevation | 84° | 80° | 76° | 71° | 66° | 58° |

(Reading a screen length r as the ground shadow, as this page once did with
"42°" for 1.1, ignores the view's foreshortening.) On the ground the shadows
fall at 57° south of east; on screen at 53°, along (0.6, 0.8).

Throws are capped at 45 native pixels, under the explorers' `flightHeight` of
52, so flying units still read as above everything. The classic layer is the HD
layer area-averaged, so classic and HD shadows match. Damaged variants share
their building's mask where their geometry matches. The tool also removes the
shadows the old art baked in: only near-black, semi-transparent base pixels
(alpha < 220, every channel < 45), so translucent art such as the barracks
blade stays.

## Known exceptions

These do not follow the sun yet and should be fixed when the art is next
reworked:

- Swarm construction sites (team layer only), flags and walls carry no
  ground shadow layer yet.
- The explorer's own render is lit almost from the front and above (its key
  lamp sits near the camera); papyrus is faintly lit from the upper right.
- Hedge and thicket canopies can only drift their long shadow 0.3 pixels right
  per pixel down (about 73° below horizontal): a connected frame sees just
  `HALO` pixels of the canopy to its left, so a wider throw would stop square at
  cell edges.
- The web site and online studio use straight-down CSS shadows from the design
  system; tutorial arrows 39 and 43 are rotated copies of one shaded sprite and
  so read as lit from the lower left.
