# Clearing flags must not index fruit as basic resources

Clearing flags have five resource switches (`BASIC_COUNT`), while cherries,
oranges and prunes have resource IDs 5, 6 and 7. Both local and global gradient
builders previously used any nonempty resource ID to index that five-element
array. Fruit therefore read beyond the array into object padding. Different
allocation histories, including loading a saved game, could change which tiles
were treated as clearing targets and produce different unit paths.

Both builders now require a basic resource ID before accessing the switches.
Fruit remains an obstacle; clearing flags do not support fruit-clearing switches.
No save-format or AI configuration change is needed.

## Reproduce and verify (Linux)

Install the normal SCons/C++20 game build dependencies, including the SDL2,
SDL2_net, SDL2_ttf, SDL2_image, Vorbis, Speex, FriBidi, epoxy, Boost date-time,
zlib, OpenGL and GLU development packages and pkg-config. From the repository:

```sh
scons --build=build-pr release=1 server=0 CXXFLAGS= LINKFLAGS= -j4 build-pr/src/glob2
python3 test/run_clearing_regression.py --build-dir build-pr
```

The native fixture creates a clearing flag and places each resource type beneath
it. It varies only the alignment padding following the five resource switches
between zero and one, simulating different allocation histories without changing
game state. On the affected Linux layout, the old code incorrectly marks fruit
as a goal when the padding is one, causing the assertion to fail.

The corrected engine passes checks for both local and global gradients, all
three fruit types, each enabled/disabled basic-resource switch, empty tiles,
and both swimming variants. No display, AI implementation or saved tournament
file is required. Expected final output: `ClearingFlagGradientTest: ... PASS`.
The test must be linked against freshly rebuilt objects after source changes.
