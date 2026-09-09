# High-resolution rendering and map zoom

Part 1 of 3: renderer and camera only. No replacement artwork or AI tooling.
Part 2 supplies recovered original artwork; part 3 supplies optional AI-derived
finals. Missing packs/frames use classic artwork.

The renderer keeps logical sprite geometry separate from texture dimensions.
OpenGL uses high-resolution textures, mipmaps and lazy team-color caches when
a pack is available. Gameplay, replays and editor support 50–300% zoom via
Alt+wheel and visible controls; wrapped drawing and picking agree. UI size is
independent of map zoom. Software stays at 100% with classic assets.
HD preference defaults on, with a classic switch; no pack is bundled here.

Build: `scons release=1 -j8 build/src/glob2 highres-integration-test`.
Run `build/src/HighResolutionIntegrationHarness` and its `software` mode.
The harness exercises camera/picking, order isolation, replay checksums and
missing-pack fallback. `test/RuntimePackCheck.cpp` is for a supplied pack and
is exercised by the subsequent artwork PRs.

No simulation, save or network state is added by zoom. The toroidal map repeats
to fill the viewport. GPU/software fallback remains available.
