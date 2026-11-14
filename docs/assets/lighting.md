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
| Sun elevation | about 42° above the ground; as a 3D vector toward the light, normalize(−0.62, −0.78, 0.9) |
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
| Runtime (C++) | `src/render/Sunlight.h` | Cloud shadows, flying units' shadows and the torus specular sun. Use it for any new runtime shading. |
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

## Known exceptions

These do not follow the sun yet and should be fixed when the art is next
reworked:

- Large classic buildings (hospital, school, inn, racetrack, defence tower,
  market, pool, barracks) cast shadows straight right or up-right, as if lit
  from the left or lower left. The high-resolution frames inherit this from the
  classic art.
- Wheat dots are highlighted on the upper left but cast their shadows up-right.
- The explorer's own render is lit almost from the front and above (its key
  lamp sits near the camera); papyrus is faintly lit from the upper right.
- Hedge and thicket canopies can only drift their long shadow 0.3 pixels right
  per pixel down (about 73° below horizontal): a connected frame sees just
  `HALO` pixels of the canopy to its left, so a wider throw would stop square at
  cell edges.
- The web site and online studio use straight-down CSS shadows from the design
  system; tutorial arrows 39 and 43 are rotated copies of one shaded sprite and
  so read as lit from the lower left.
