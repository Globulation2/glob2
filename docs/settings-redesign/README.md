# Main Settings redesign

Native C++/SDL captures from `test/run-settings-tests.py` using disposable profiles.
The game remains on the existing front-end theme. Only the main Settings form is
reorganized; the in-game options dialog is unchanged.

- `display.png`: Display & graphics at 1000×700.
- `renderer.png`: Renderer visible beneath the graphics settings, with no expander.
- `dropdown-640.png`: Window-mode dropdown at 640×480, with the page still visible.
- `buildings.png`: Completed-building defaults and level columns.
- `controls.png`: Actions with existing, additional, or unbound shortcuts.
- `binding.png`: Advanced sequence and press/release editing.

The OpenGL runs reported 2× drawable dimensions. Captures are exported at logical
window size by the native screenshot implementation.

## Validation

```sh
scons -j6 release=1 build/src/glob2 settings-tests speed-tests
python3 test/run-settings-tests.py
python3 test/run-game-speed-tests.py --settings-only
```

The final settings run passed at 640×480, 800×600, 1000×700, and 1280×900 in
OpenGL, plus 1000×700 software and 640×480 with expanded English strings.
The harness also exercises French, checked autosave/retry, software display
Keep/Revert/timeout/failure, OpenGL pending changes, building ranges and default
radius preservation, shortcut sequences/conflicts, dropdown interaction, and
preservation of artwork/torus preferences. The focused game-speed run covers
in-game options, multiplayer/replay eligibility, default shortcuts, and camera cadence.

The full engine/replay runner previously hung during playback (also reproduced
with the original settings implementation). Its passing settings-focused subset
is reported separately; a complete engine/replay pass is not claimed here.
