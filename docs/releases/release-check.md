# Release check

The release check is a scripted run of the paths most players take, played by
the game itself before a build is published. It drives the same screens and
calls a player's taps reach (`src/app/ReleaseCheck.h`), so a release that
cannot start a match, load a save or reach its results screen fails its
release job rather than reaching players.

## Steps

Each step starts and ends at the main menu.

| Step | What it does |
| --- | --- |
| `menu` | Reaches the main menu with first-run prompts already answered |
| `custom` | Opens the custom game setup, launches a match, plays 100 ticks and quits |
| `load` | Loads `data/release-check/victory.game.gz`, plays 100 ticks and quits |
| `lesson` | Starts the walking lesson (`campaigns/walking-training.map.gz`), plays and quits |
| `tutorial` | Starts the tutorial's introduction map, plays and quits |
| `victory` | Plays the same save to its win, opens every results view, saves the replay to `replays/release-check.replay` and returns to the menu |

The game logs `Glob2 release check: <step> ok` after each step and ends with
`Glob2 release check passed` (exit 0) or `Glob2 release check failed: <why>`
(exit 1). A step has ten minutes. A crash ends the check, and the crash capture
keeps it like a player's crash. The check turns off autosaves and plays at
maximum game speed; it changes no saved settings.

## Starting it

`GLOB2_RELEASE_CHECK` set to `1` or `all` runs every step; a comma-separated list
(`menu,victory`) runs those steps. Each platform passes it in its own way:

| Platform | How | Runner |
| --- | --- | --- |
| Linux, Windows, macOS | environment variable | `python3 tools/release/release_check.py <binary>` |
| Android | intent extra `glob2.releaseCheck` | `python3 platforms/mobile/smoke.py --release-check` |
| iOS simulator | `SIMCTL_CHILD_GLOB2_RELEASE_CHECK` | `python3 platforms/mobile/ios_smoke.py --release-check` |
| Browser | `?release-check=all` | `GLOB2_RELEASE_CHECK_SPEC=1 npx playwright test release-check.spec.js` |

`release_check.py` uses a scratch profile, dummy audio and, on Linux, SDL's
offscreen video, and copies the game's output and any kept crash to `--output`
(default `artifacts/release-check`).

## Where it runs

- `release.yml` runs it on the Linux, Windows (the unpacked release zip) and
  macOS packages and in the Android emulator smoke; a failure uploads
  `release-check-<platform>`. Tags cut before the check existed skip it.
- The `linux-variants` job of `build.yml` runs it on a release build, with the
  save check below.
- The browser matrix has a `chromium release check` entry.
- The iOS runner is manual for now (`ios_smoke.py --release-check`).

## The victory save

`content/data/release-check/victory.game.gz` is an AI match (warrush against
numbi on SmallForTwo, seed 1) saved a few hundred ticks before team 0 wins;
`victory.json` records the saved tick and the win. A simulation change can move
that ending. Check the save with

```bash
python3 tools/release/make_release_check_save.py --glob2 build/linux/client/release/src/glob2 --verify
```

and, when it no longer ends in a team 0 win within 2000 ticks, regenerate it in
the same change that bumps `SIM_REVISION`:

```bash
python3 tools/release/make_release_check_save.py --glob2 build/linux/client/release/src/glob2
```

[Release documentation](README.md).
