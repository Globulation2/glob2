# Building header placement evidence

Source: `56b988db091377687754ed34cda2ffc1ff01970d`. Base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

Native Linux release engine tests, SDL 3.4.16 / GCC 15.2, portable GPU under Xvfb. All four GameGUITouch cases pass. See after.xml, after.log, and gameplay-output.txt for source/compiler provenance.

Build: `GLOB2_SDL3_PREFIX=<native SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests`

Run: `env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/building-header/evidence/after.xml --verbose`

The gameplay fixture asserts that the building header has exactly the rendered stats bounds horizontally, is below the stats and left of the minimap, and shares the minimap bottom edge. Real SDL taps exercise the moved close button without orders. Mirroring the thumb preference must leave the header aligned with stats. Existing allocation, palette, brush, flag, map gesture and dialog checks pass.

Screenshots cover 320×568 portrait, 568×320 landscape and 844×390 wide landscape, each with both thumb preferences. The short two-row stats layout uses a larger minimap while inspecting a building to leave room for the header. Other tool modes retain the previous minimap size. The icon is reduced to fit the narrower title bar; identity, HP and ownership text remain visible.

Physical Android/iOS testing is still needed for on-device feel. No simulation or persistence format changes.
