# Colony video review evidence

App PR: https://github.com/Globulation2/glob2/pull/745
Website PR: https://github.com/Globulation2/glob2-online-website/pull/9

Website tested commit: `0a06603ded5b94dd4cc952abd503cb3897be789c`.
Website base: `16f5f6b7372f0e83addaf76890109e2fc911ba97` (unchanged).
Environment: macOS 26.6.2, arm64, Node 24.14.0; pinned npm lockfiles;
Playwright 1.63.0 with installed Chromium, Firefox and WebKit.

Website commands (all exit 0):

```sh
npm run check
npm run build
node scripts/check-links.mjs
node --test scripts/test_monitor.mjs
python3 -m unittest discover -s scripts -p 'test_*.py' -v
npm test
```

Website browser result: 114 passed across three browser engines. Source checks:
42 files, zero errors/warnings/hints. Link/media check: 26 pages. Python: 21 tests.
Two independent rendered review rounds checked 1470/320/430px in light and dark:
real playback, pause, readable layout without overlap or overflow, reduced motion
with no MP4 requests, and failed playback retaining the poster and hiding the
control. Screenshots are in `website-review/`; logs are in `logs/`.

Native capture and initial app validation used base
`87a8b2528` with the exact committed colony-video patch before rebasing.
The video is 1600×900, approximately 60 seconds, H.264/yuv420p, silent,
6,831,278 bytes. SHA-256:
`f898edeb338efca294c1e12d1c7291c36c04f138906c5a123c2748751db28745`.
Both repositories contain identical video and poster assets. No generated game
imagery or separate unit animations are used.

Original native build/capture commands:

```sh
GLOB2_SDL3_PREFIX=/Users/bradley/.cache/glob2-sdl3/prefix scons --build=build/native-tests -j10 release=1 server=0 menu-colony-harness
SDL_VIDEODRIVER=dummy build/native-tests/test/MenuColonyHarness check data/menu/colony.bin
SDL_VIDEODRIVER=dummy python3 tools/record_menu_colony.py --harness build/native-tests/test/MenuColonyHarness
python3 tools/record_menu_colony.py --encode-only --ffmpeg <isolated-imageio-ffmpeg-7.1>
```

The system FFmpeg binary had a missing dylib; lossless captured frames were
encoded with imageio-ffmpeg 0.6.0's isolated arm64 FFmpeg 7.1 binary instead.
Native check passed presentation, timing, isolation, determinism, scoped style
and fallback contracts. Native renderer defaults and simulation behavior are
unchanged; only capture can opt out of status bars. No save/replay/network or
simulation-version gates change. Windows/Linux/native-GPU matrices were not
run for this presentation-only patch. The three-browser website checks cover
MP4 playback compatibility; they do not establish native platform equivalence.

App integration validation against refreshed master is recorded separately once
complete. No changes to the video or website sources occurred during that refresh.

Author accepts website coverage as sufficient for this revision under AGENTS.md;
the user explicitly requested commit, push and merge after reviewing the result.
