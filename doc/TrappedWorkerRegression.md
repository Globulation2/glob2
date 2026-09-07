# Workers blocked inside buildings must eventually starve

After feeding or training finishes, a unit enters `DIS_EXITING_BUILDING`.
If wood or wheat blocks every exit, movement remains `MOV_INSIDE`. Previously
`handleMedical()` returned for this state forever: hunger never decreased, so
an otherwise defeated team could remain alive indefinitely.

The fix resumes hunger only after service has finished and the unit is waiting
inside for an exit. It retains the building subscription until escape or death.
An indoor death removes the subscription without clearing a map occupancy slot
or generating an outdoor death animation. Active service still pauses hunger.

## Reproduce and verify (Linux)

Install the normal SCons/C++20 game build dependencies, including the SDL2,
SDL2_net, SDL2_ttf, SDL2_image, Vorbis, Speex, FriBidi, epoxy, Boost date-time,
zlib, OpenGL and GLU development packages and pkg-config. From the repository:

```sh
scons --build=build-pr release=1 server=0 CXXFLAGS= LINKFLAGS= -j4 build-pr/src/glob2
python3 test/run_trapped_regression.py --build-dir build-pr
```

No display or tournament assets are required. The fixture creates a small map,
an inn containing a worker whose service has ended, and a complete ring of wood
or wheat around the exits. With hunger 10 and consumption 2, the first medical
step must reduce hunger to 8. Before the fix it remains 10 and the assertion
fails. Continuing medical steps must kill the worker, remove its building
subscription and allow the ordinary team-loss/winner rules to finish the game.

The same executable also checks clearing an exit before starvation, preserving
hunger during active service, and continuing starvation through a save/load.
The final output is `TrappedUnitLifecycleTest: ... PASS`.

This fixture uses the ordinary engine and generated in-memory saves. It does
not require Maxima, experimental save formats, or private tournament checkpoints.
The test must be linked against freshly rebuilt objects after source changes.
