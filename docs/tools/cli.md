# Glob2 command-line guide

The main `glob2` executable launches the game and provides map, simulation,
validation, rendering, and developer tools. This guide describes **CLI version 2**.
The CLI version is independent of the simulation identity, save version, replay
version, and JSON payload versions.

## Getting started

```sh
glob2                                      # open the game
glob2 play --window-size 1280x720 --no-fullscreen
glob2 --help                               # discover every command family
glob2 map --help                           # discover map tools
glob2 help map generate                    # full command help
glob2 help --format json                   # static machine-readable description
glob2 map generators river                 # available generator controls
```

No-argument launching and supported bare `glob2://` or HTTPS invite/catalog URLs
remain available for desktop launchers. Other workflows use explicit commands.
The `glob2-server` executable and separate test/study binaries have their own
interfaces; this guide does not change them.

Help works without game data, a display, a profile, or network access. Leaf help
lists positional inputs, options, defaults, units, repeatability, constraints,
outputs, platform requirements, exit codes, and examples. Dynamic catalogs need
installed game data; `info catalog --format json` preserves the existing domain
catalog. `help --format json` is the separate static command-description schema.
Unavailable commands remain described with their platform requirements.

## Common workflows

Generate a map at one exact seed, then inspect it without running simulation:

```sh
glob2 map generate river --seed 713 --width 128 --height 128 --teams 2 \
  --output artifacts/river.map --preview artifacts/river.png \
  --report-file artifacts/river.json
glob2 map preview artifacts/river.map.gz --output artifacts/reloaded.png
```

Map output is gzip-compressed; `.gz` is appended unless present. Loading a bare
`.map` or `.game` path prefers its existing `.gz` sibling. A preview is the lobby's
overview representation; `map render` exports the full game view. Categorical
image import/export preserves geography and colony markers, not a complete save.
See the [map CLI guide](../map-generators/cli.md) for configuration and image rules.

Structured map studies retain candidate-search, rotations, and job artifacts:

```sh
glob2 map study river --seed 713 --set width=7 --set height=7 --set teams=2 \
  --write-map --output-dir artifacts/study
```

`map generate` controls use displayed values, including width/height in tiles.
Structured `map study` and `game run --generator` retain domain-catalog control
values: width/height are exponents, so `7` means 128 tiles. Discover those values
through `info catalog --format json`. Direct generation runs exactly the requested
seed and does not silently search for another layout.

Run a bounded headless game or continue a saved game:

```sh
glob2 game run --map-file artifacts/river.map.gz --game-seed 713 \
  --player castor --player cortex --ticks 64 --write-replay \
  --telemetry checksums --save final --output-dir artifacts/game
glob2 game run --load-game artifacts/game/final.game.gz --ticks 128 \
  --telemetry checksums --output-dir artifacts/continued
glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr \
  --map-file maps/FourSquares1.map.gz --output-dir artifacts/verified
```

Each structured job needs a fresh output directory without `result.json`. A saved
match carries its setup; continuation rejects new-match player, seed, rule, and
script overrides. `--ticks` is the absolute stopping tick. A verification command
reports its domain verdict in JSON; successful execution alone is not a verified
verdict. See [headless execution and replays](../development/headless-replays.md).

```sh
glob2 ai check examples/javascript/ai.js --format json
glob2 online join ABCDEF --instance https://app.glob2online.com
glob2 info version
glob2 info sim-version --format json
```

## Argument and output rules

- Put the command path first, then options and positional arguments. Options may
  occur before or after a command's positional inputs.
- Use `--option VALUE` or `--option=VALUE`. Boolean switches take no value.
- Scalar options cannot repeat. Repeatable options preserve their order, including
  team/player assignment order. A switch conflicts with its `--no-` counterpart.
- Use `--` before positional filenames starting with `-`; use `--option=-VALUE`
  for a string option whose value starts with `-`. Quote paths containing spaces.
- Numbers must parse completely and remain within the documented range. Unknown
  commands/options, missing values, conflicts, and trailing arguments fail clearly.
- CLI syntax validation precedes job-directory creation. Execution-time failures
  retain the existing structured result/report behavior where the job supports it.
- Inspection commands default to readable text. Use `--format json` for automation.
  JSON stdout contains the payload; diagnostics go to stderr.
- Exit codes are `0` for success/help, `2` for invalid arguments or rejected input,
  and `3` for operational failures. Domain verdicts and validation results remain
  in their existing JSON payloads.

## Paths, profiles, environment, and platforms

Tool input/output paths are relative to the invoking process's working directory,
including macOS app-bundle tools. Game launching locates bundle assets separately.
`--data-dir DIRECTORY` adds asset search directories in order; it does not change
where output files are written. Use `info paths` to inspect search paths.

Structured jobs create isolated profiles below their output directory and clear
engine-affecting tuning/diagnostic environment overrides. Their explicit options
control match setup and telemetry. Developer random-game and repeated-game modes
retain their existing environment-driven diagnostics; their command help identifies
applicable variables. For profiles, tuning, and platform environment switches, see
[development environment](../development/README.md) and the relevant domain guide.

Native, browser, and mobile builds share command definitions. Capability metadata
identifies platform restrictions; native skin exporting requires OpenGL. Help and
completion never initialize graphics, load packages, or make online requests.
Browser hosts continue to await `Module.start(args)`; their argument arrays use the
same command paths. No new runtime dependency is introduced.

## Shell completion and manuals

```sh
# Bash: source in the current shell, or save in your completion directory.
source <(glob2 completion bash)
# Zsh: save _glob2 in a directory on $fpath, then initialize compinit.
glob2 completion zsh > ~/.zsh/completions/_glob2
# Fish: install in the per-user completion directory.
glob2 completion fish > ~/.config/fish/completions/glob2.fish
```

Create destination directories before saving. Native packages ship generated
completion scripts and the `glob2(6)` manual. Completion covers command paths,
option names, static choices, and filesystem paths without executing jobs or
contacting services.

Maintainers regenerate the reference, manual, and completion files from a built
production client with `python3 tools/cli_reference.py --binary /path/to/glob2`.
Run the same command with `--check` to detect drift. Edit the C++ registry and the
hand-written workflow sections instead of editing generated reference material.

## Migrating from CLI 1

CLI 2 is a deliberate clean break: old spellings are rejected with replacement
hints. `-h` now means help; use `play --graphics-detail full` for its former behavior.
Update binaries, engine/skin workers, tournament bundles, and browser callers
as one release. Workers probe the static description and require CLI 2. Re-register
old tournament bundles; no old-binary adapter is provided.

The command change does not require changing saves, match records, replays, or
production data. Existing JSON artifact schemas and simulation identity remain
separate compatibility contracts. Recorded fixture provenance and explicitly
historical documents may show the command that originally produced their data.

<!-- BEGIN GENERATED CLI MIGRATION -->

| Previous spelling | CLI 2 replacement |
| --- | --- |
| `--ai-threads` | `--compute-threads auto|N` |
| `--attach-map-script` | `script attach` |
| `--check-ai` | `ai check` |
| `--check-ai-json` | `ai check --format json` |
| `--check-script` | `script check` |
| `--compose-buildings` | `assets compose-buildings` |
| `--compute-experiments` | `--compute-threads auto|N` |
| `--export-map-image` | `map export-image` |
| `--generate-map` | `map generate (direct files) or map study (structured jobs)` |
| `--gradient-workers` | `--compute-threads auto|N` |
| `--headless-catalog` | `info catalog --format json` |
| `--hive-worker` | `dev hive-worker` |
| `--import-map-image` | `map import-image` |
| `--inspect-generator-package` | `map inspect-package` |
| `--join` | `online join` |
| `--json` | `--report-file` |
| `--list-map-generators` | `map generators` |
| `--local-map` | `online play-map` |
| `--map` | `--map-file (except dev random-games --map NAME)` |
| `--nox` | `game repeat` |
| `--out` | `--output-dir` |
| `--param` | `--set` |
| `--preview-map` | `map preview` |
| `--record-ffmpeg` | `--record-encoder` |
| `--record-size` | `--record (full framebuffer resolution)` |
| `--render-game` | `map render` |
| `--render-skin` | `assets render-skin` |
| `--replay` | `--write-replay` |
| `--room-map` | `online host-map` |
| `--run-game` | `game run` |
| `--sim-version` | `info sim-version --format json` |
| `--skin-render-info` | `assets skin-info` |
| `--turn-client` | `online turn-client` |
| `--validate-set` | `map validate-set` |
| `--verify-match` | `match verify` |
| `--version` | `info version` |
| `-C` | `--no-custom-cursor` |
| `-F` | `--no-fullscreen` |
| `-G` | `--renderer software` |
| `-M` | `--no-mute` |
| `-R` | `--no-resizable` |
| `-c` | `--custom-cursor` |
| `-d` | `--data-dir` |
| `-dl` | `info paths` |
| `-dump-resources` | `dev dump-resources` |
| `-dump-tiled` | `dev dump-tiled` |
| `-dump-wheat` | `dev dump-wheat` |
| `-f` | `--fullscreen` |
| `-g` | `--renderer gpu` |
| `-l` | `--graphics-detail reduced` |
| `-m` | `--mute` |
| `-nox` | `game repeat` |
| `-r` | `--resizable` |
| `-replay` | `replay` |
| `-s` | `--window-size` |
| `-sgsl` | `--editor-script sgsl` |
| `-test-games` | `dev random-games --display` |
| `-test-games-nox` | `dev random-games` |
| `-test-map-gen` | `dev stress-maps` |
| `-textshot` | `dev textshots` |
| `-u` | `--username` |
| `-usl` | `--editor-script usl` |
| `-version` | `info version` |
| `-vs` | `--videoshot` |
| `/?` | `--help` |

<!-- END GENERATED CLI MIGRATION -->

## Complete command reference

<!-- BEGIN GENERATED CLI REFERENCE -->

### `glob2 play`

Launch the graphical game.

```text
glob2 play [OPTIONS]
```

Outputs: Interactive game.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 play --window-size 1280x720 --no-fullscreen
```

### `glob2 replay`

Watch a recorded replay.

```text
glob2 replay FILE [OPTIONS]
```

Outputs: Interactive replay.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 replay replays/match.replay
```

### `glob2 map generate`

Generate a map at one exact seed.

```text
glob2 map generate GENERATOR [OPTIONS]
```

Outputs: Requested .map.gz, overview PNG, categorical PNG and JSON report.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--seed` | integer; default 1; 0..4294967295 | Map seed (unsigned 32-bit). |
| `--set` | assignment | Generator control KEY=VALUE; CLI overrides config. Repeatable. |
| `--width` | integer; 1..512 | Map width in tiles; registered values only. |
| `--height` | integer; 1..512 | Map height in tiles; registered values only. |
| `--teams` | integer; 1..32 | Number of colonies; registered values only. |
| `--workers` | integer; 0..2147483647 | Initial workers per colony; registered values only. |
| `--preview` | file | Overview PNG output. |
| `--preview-size` | integer; pixels; 128..4096 | Longest preview side in pixels. |
| `--preview-scale` | 2, 4, 8; default 2 | Scale retained thumbnail pixels. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--output` | file | Playable gzip map output; .gz is appended. |
| `--report-file` | file | Detailed map JSON report. |
| `--config` | file | KEY=VALUE configuration; CLI settings override it. |
| `--map-image` | file | Export categorical terrain PNG. |
| `--export-generator-package` | file | Export selected custom generator package. |
| `--help` | flag; default false | Show this command's help (also -h). |

At least one output is required.

Registered generator controls constrain tile sizes and values.

A failed layout never silently retries with another seed.

Choose at most one: `--preview-scale`, `--preview-size`.

```sh
glob2 map generate river --seed 713 --width 128 --height 128 --teams 2 --output artifacts/river.map --preview artifacts/river.png
```

### `glob2 map study`

Run a structured map-generation study.

```text
glob2 map study GENERATOR [OPTIONS]
```

Outputs: result.json, progress.json, artifacts.json; optional maps and reports.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--output-dir` | directory | Structured job directory; must not already contain result.json. Required. |
| `--profile` | string; default glob2-tournament | Isolated profile label. |
| `--building-catalog` | file | Building catalog manifest. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--seed` | integer; default 1; 0..4294967295 | Map seed. |
| `--set` | assignment | Catalog control KEY=VALUE (study width/height use catalog exponents). Repeatable. |
| `--candidates` | integer; default 0; 0..10000 | Search candidate count; 0 uses the exact seed. |
| `--rotations` | integer; default 1; 1..16 | Team rotations. |
| `--write-map` | flag; default false | Write chosen map-r0.map.gz. |
| `--report` | headroom, diagnostics, timing, terrain | Additional report. Repeatable. |
| `--perturb` | string | Study perturbation specification. Repeatable. |
| `--building-artwork` | file | Building artwork bundle. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 map study river --seed 713 --write-map --output-dir artifacts/study
```

### `glob2 map generators`

List generators or inspect controls.

```text
glob2 map generators [GENERATOR] [OPTIONS]
```

Outputs: Generator IDs, defaults and allowed control values.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 map generators river
```

### `glob2 map preview`

Preview a map or save without stepping simulation.

```text
glob2 map preview FILE [OPTIONS]
```

Outputs: Overview PNG and/or map report.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--output` | file | PNG output. |
| `--report-file` | file | Map analysis JSON report. |
| `--preview` | file | Overview PNG output. |
| `--preview-size` | integer; pixels; 128..4096 | Longest preview side in pixels. |
| `--preview-scale` | 2, 4, 8; default 2 | Scale retained thumbnail pixels. |
| `--help` | flag; default false | Show this command's help (also -h). |

Choose at most one: `--output`, `--preview`.

Choose at most one: `--preview-scale`, `--preview-size`.

```sh
glob2 map preview maps/FourSquares1.map.gz --output artifacts/map.png
```

### `glob2 map render`

Render the complete game view.

```text
glob2 map render FILE [OPTIONS]
```

Outputs: PNG; requires installed game graphics.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--output` | file | Game-view PNG output. Required. |
| `--render-max-pixels` | integer; default 4096; pixels; 1..8192 | Maximum side in pixels. |
| `--render-field` | file | Diagnostic field overlay. |
| `--field-color` | string | Field color as r,g,b. |
| `--help` | flag; default false | Show this command's help (also -h). |

`--field-color` requires `--render-field`.

```sh
glob2 map render maps/FourSquares1.map.gz --output artifacts/game-view.png
```

### `glob2 map import-image`

Import a categorical terrain PNG.

```text
glob2 map import-image FILE [OPTIONS]
```

Outputs: Playable .map.gz and optional preview/report.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--seed` | integer; default 1; 0..4294967295 | Map seed (unsigned 32-bit). |
| `--set` | assignment | Generator control KEY=VALUE; CLI overrides config. Repeatable. |
| `--width` | integer; 1..512 | Map width in tiles; registered values only. |
| `--height` | integer; 1..512 | Map height in tiles; registered values only. |
| `--teams` | integer; 1..32 | Number of colonies; registered values only. |
| `--workers` | integer; 0..2147483647 | Initial workers per colony; registered values only. |
| `--preview` | file | Overview PNG output. |
| `--preview-size` | integer; pixels; 128..4096 | Longest preview side in pixels. |
| `--preview-scale` | 2, 4, 8; default 2 | Scale retained thumbnail pixels. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--output` | file | Playable gzip map output. Required. |
| `--report-file` | file | Import report JSON. |
| `--image-seam-width` | integer; 0..16 | Seam repair width in tiles; default short side / 32 clamped 2..12. |
| `--help` | flag; default false | Show this command's help (also -h). |

Choose at most one: `--preview-scale`, `--preview-size`.

```sh
glob2 map import-image terrain.png --width 128 --height 128 --output artifacts/imported.map
```

### `glob2 map export-image`

Export categorical terrain and colony markers.

```text
glob2 map export-image FILE [OPTIONS]
```

Outputs: Categorical PNG; not an exact save-state interchange.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--output` | file | Categorical PNG output. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 map export-image maps/FourSquares1.map.gz --output artifacts/terrain.png
```

### `glob2 map inspect-package`

Inspect and canonicalize a generator package.

```text
glob2 map inspect-package FILE [OPTIONS]
```

Outputs: Canonical package and metadata JSON.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--output` | file | Canonical package JSON. Required. |
| `--report-file` | file | Metadata JSON. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 map inspect-package generator.json --output artifacts/canonical.json --report-file artifacts/package.json
```

### `glob2 map validate-set`

Validate a terrain/resource set.

```text
glob2 map validate-set FILE [OPTIONS]
```

Outputs: Validation JSON and optional PNG.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--report-file` | file | Validation JSON. Required. |
| `--preview` | file | Preview PNG. |
| `--gallery` | 0, 1; default 0 | Preview gallery. |
| `--phase` | integer; default 0; 0..3 | Gallery phase. |
| `--variation` | integer; default 0; 0..3 | Gallery variation. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 map validate-set set.json --report-file artifacts/set-report.json --preview artifacts/set.png
```

### `glob2 game run`

Run or continue a structured headless game.

```text
glob2 game run [OPTIONS]
```

Outputs: result.json, progress.json, artifacts.json; requested saves, replay and telemetry.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--output-dir` | directory | Structured job directory; must not already contain result.json. Required. |
| `--profile` | string; default glob2-tournament | Isolated profile label. |
| `--building-catalog` | file | Building catalog manifest. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--map-file` | file | Map input for a new game. |
| `--load-game` | file | Saved-game input for continuation. |
| `--generator` | string | Generate a map before starting. |
| `--game-seed` | integer; 0..4294967295 | Game seed for a new match. |
| `--map-seed` | integer; 0..4294967295 | Generated-map seed. |
| `--set` | assignment | Generator control KEY=VALUE. Repeatable. |
| `--candidates` | integer; default 0; 0..10000 | Generation candidate count. |
| `--player` | string | AI per map team in team order. Repeatable. |
| `--ai-param` | assignment | AI override PLAYER:KEY=VALUE. Repeatable. |
| `--ai-script` | string | JavaScript AI PLAYER:SOURCE.js. Repeatable. |
| `--map-script` | file | Map script source. |
| `--alliance` | integer; 1..16 | Alliance per team. Repeatable. |
| `--win-condition` | death, allies, prestige, opponents, script | Winning condition. Repeatable. |
| `--win-probability` | integer; 501..1000 | Early-victory threshold in permille. |
| `--experiment` | string | Enable a registered experiment. Repeatable. |
| `--rule` | assignment | Game rule KEY=VALUE. Repeatable. |
| `--fork-rule` | assignment | Explicit saved-game fork: buildingGradientDelay=N. Repeatable. |
| `--ticks` | integer; default 90000; ticks; 1..2147483647 | Absolute tick limit; must exceed saved tick. |
| `--gradient-delay` | integer; default 8; ticks; 1..16 | Gradient scheduling delay in ticks. |
| `--resource-growth-delay` | integer; ticks; 1..16 | Resource scheduling delay in ticks. |
| `--ai-order-delay` | integer; ticks; 0..8 | AI order delay in ticks. |
| `--save` | string | Snapshot: initial, final, or every:N. Repeatable. |
| `--telemetry` | checksums, team-timeline, maxima, gradient-stats | Additional telemetry. Repeatable. |
| `--write-replay` | flag; default false | Write game.replay. |
| `--benchmark-warmup` | integer; ticks; 0..2147483647 | Ticks excluded from benchmark counters. |
| `--diagnostic-fields` | maxima | Diagnostic field selection. |
| `--diagnostic-interval` | integer; default 2500; ticks; 1..2147483647 | Diagnostic interval in ticks. |
| `--diagnostic-png` | flag; default false | Export diagnostic PNGs. |
| `--building-artwork` | file | Artwork for generated maps. |
| `--help` | flag; default false | Show this command's help (also -h). |

Exactly one of --map-file, --load-game or --generator.

New games require --game-seed and one --player per team.

Generation requires --map-seed.

Saved games reject new-match setup options.

Structured jobs isolate engine tuning and diagnostic environment variables.

`--building-artwork` requires `--generator`.

`--candidates` requires `--generator`.

`--diagnostic-interval` requires `--diagnostic-fields`.

`--diagnostic-png` requires `--diagnostic-fields`.

`--fork-rule` requires `--load-game`.

`--map-seed` requires `--generator`.

`--set` requires `--generator`.

```sh
glob2 game run --map-file maps/FourSquares1.map.gz --game-seed 713 --player castor --player cortex --player castor --player cortex --ticks 64 --telemetry checksums --output-dir artifacts/game
```

### `glob2 game repeat`

Repeat a saved game headlessly.

```text
glob2 game repeat FILE [OPTIONS]
```

Outputs: Existing game summaries and opt-in environment telemetry.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--ticks` | integer; default 0; ticks; 0..2147483647 | Steps per run; 0 runs until the game ends. |
| `--runs` | integer; default 1; 1..2147483647 | Number of saved-game runs. |
| `--building-catalog` | file | Building catalog. |
| `--help` | flag; default false | Show this command's help (also -h). |

Applicable environment variables: `GLOB2_REPLAY_PATH`, `GLOB2_CHECKSUM_SIDECAR`, `GLOB2_TEAM_RESULTS`, `GLOB2_TEAM_TIMELINE`.

```sh
glob2 game repeat saved.game.gz --ticks 64 --runs 1
```

### `glob2 match verify`

Verify a multiplayer match record.

```text
glob2 match verify RECORD [OPTIONS]
```

Outputs: result.json, verdict.json, checksums.txt, match.replay, compute.json, artifacts.json; verdict is in JSON.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--output-dir` | directory | Structured job directory; must not already contain result.json. Required. |
| `--profile` | string; default glob2-verify | Isolated profile label. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--map-file` | file | Recorded match's map. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr --map-file maps/FourSquares1.map.gz --output-dir artifacts/verified
```

### `glob2 online join`

Join an invite link or code.

```text
glob2 online join INVITE [OPTIONS]
```

Outputs: Interactive online lobby.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--instance` | string | Online instance origin. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 online join ABCDEF --instance https://app.glob2online.com
```

### `glob2 online play-map`

Play a catalog map locally.

```text
glob2 online play-map ID [OPTIONS]
```

Outputs: Interactive custom-game setup.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--instance` | string | Online instance origin. |
| `--hash` | string | Catalog map SHA-256. Required. |
| `--title` | string | Catalog map title. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 online play-map MAP_ID --hash SHA256 --title 'Catalog map'
```

### `glob2 online host-map`

Host a catalog map online.

```text
glob2 online host-map ID [OPTIONS]
```

Outputs: Interactive room creation.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--instance` | string | Online instance origin. |
| `--hash` | string | Catalog map SHA-256. Required. |
| `--title` | string | Catalog map title. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 online host-map MAP_ID --hash SHA256 --title 'Catalog map'
```

### `glob2 online turn-client`

Play a relay assignment headlessly.

```text
glob2 online turn-client ASSIGNMENT [OPTIONS]
```

Outputs: result.json, network/trace artifacts and replay.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--output-dir` | directory | Structured job directory; must not already contain result.json. Required. |
| `--profile` | string; default glob2-turn-client | Isolated profile label. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--map-file` | file | Recorded match's map. Required. |
| `--orders-per-second` | real; default 0.5; 0..2147483647 | Synthetic order rate. |
| `--max-seconds` | real; default 1800; seconds; 0..2147483647 | Maximum runtime in seconds. |
| `--seed` | integer; default 1; 0..4294967295 | Synthetic-order seed. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 online turn-client assignment.json --map-file map.map.gz --output-dir artifacts/client
```

### `glob2 ai check`

Validate JavaScript AI startup, metadata and callbacks.

```text
glob2 ai check FILE [OPTIONS]
```

Outputs: Validation text or existing JSON validation payload.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 ai check examples/javascript/ai.js --format json
```

### `glob2 script check`

Compile a JavaScript map script.

```text
glob2 script check FILE [OPTIONS]
```

Outputs: Validation message.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 script check examples/javascript/scenario.js
```

### `glob2 script attach`

Attach a JavaScript map script.

```text
glob2 script attach MAP SCRIPT OUTPUT [OPTIONS]
```

Outputs: Playable .map.gz; refuses existing destination.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 script attach maps/FourSquares1.map.gz examples/javascript/scenario.js artifacts/scripted.map.gz
```

### `glob2 assets compose-buildings`

Compose and validate building packages.

```text
glob2 assets compose-buildings [OPTIONS]
```

Outputs: Resolved catalog/hash and optional artwork hash.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--base` | file | Base building catalog. |
| `--package` | file | Building package manifest. Repeatable. |
| `--artwork-bundle` | file | Building artwork bundle. |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 assets compose-buildings --format json
```

### `glob2 assets render-skin`

Bake transparent colony sprites.

```text
glob2 assets render-skin [OPTIONS]
```

Outputs: Colony sprite bundle.

Platform: native OpenGL client.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--manifest` | file | Skin manifest. Required. |
| `--texture` | file | Texture image. Required. |
| `--material` | file | Material image. Required. |
| `--output-dir` | directory | Sprite output directory. Required. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 assets render-skin --manifest skin.json --texture texture.png --material material.png --output-dir artifacts/sprites
```

### `glob2 assets skin-info`

Inspect native skin exporter capabilities.

```text
glob2 assets skin-info [OPTIONS]
```

Outputs: Exporter revision and pinned codec.

Platform: native OpenGL client.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 assets skin-info --format json
```

### `glob2 dev random-games`

Exercise random AI games.

```text
glob2 dev random-games [OPTIONS]
```

Outputs: Game summaries and opt-in diagnostic outputs.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--display` | flag; default false | Show test games graphically. |
| `--runs` | integer; default 0; 0..2147483647 | Game count; 0 repeats forever. |
| `--ticks` | integer; default 90000; ticks; 0..2147483647 | Tick cap per random game; 0 runs until game end; overrides GLOB2_TEST_MAX_TICKS. |
| `--ai-types` | string | Comma-separated random AI pool. |
| `--map` | string | Map name resolved as maps/NAME.map. |
| `--matchup` | string | Comma-separated per-team AIs; requires --map. |
| `--save-game-as` | file | Write the initial game before running. |
| `--building-catalog` | file | Building catalog manifest. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Recording requires --display.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

Choose at most one: `--matchup`, `--ai-types`.

`--matchup` requires `--map`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

Applicable environment variables: `GLOB2_TEST_SEED`, `GLOB2_TEST_MAX_TICKS`, `GLOB2_TEST_RULES`, `GLOB2_DUMP_GAME`, `GLOB2_REPLAY_PATH`, `GLOB2_CHECKSUM_SIDECAR`, `GLOB2_TEAM_RESULTS`, `GLOB2_TEAM_TIMELINE`.

```sh
glob2 dev random-games --runs 1 --ticks 64 --map Playground --matchup castor,warrush,castor,warrush,castor,warrush,castor,warrush
```

### `glob2 dev stress-maps`

Generate random maps indefinitely.

```text
glob2 dev stress-maps [OPTIONS]
```

Outputs: Generation diagnostics; interrupt to stop.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 dev stress-maps
```

### `glob2 dev textshots`

Capture rendered translation texts.

```text
glob2 dev textshots [OPTIONS]
```

Outputs: Translation screenshots; uses the existing screenshot workflow.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--fullscreen` | flag; default false | Use fullscreen. |
| `--no-fullscreen` | flag; default false | Use a window. |
| `--resizable` | flag; default false | Allow window resizing. |
| `--no-resizable` | flag; default false | Disable window resizing. |
| `--custom-cursor` | flag; default false | Use the game cursor. |
| `--no-custom-cursor` | flag; default false | Use the system cursor. |
| `--mute` | flag; default false | Mute music and speech. |
| `--no-mute` | flag; default false | Unmute music and speech. |
| `--renderer` | gpu, software | Rendering backend. |
| `--graphics-detail` | full, reduced | Detail effects. |
| `--window-size` | resolution; pixels | Initial window size in pixels; clamped to at least 640x480. |
| `--username` | string | Player name. |
| `--editor-script` | sgsl, usl; default preferences | Map editor script language. |
| `--record` | file | Record menus and gameplay to MP4; refuses existing files. |
| `--videoshot` | string | Record to videoshots/NAME.mp4. |
| `--record-fps` | integer; default 30; frames/second; 1..240 | Recording frames per second. |
| `--record-encoder` | auto, software; default auto | Embedded encoder selection. |
| `--record-crf` | integer; default 23; 0..51 | Software H.264 quality; lower means higher quality. |
| `--record-chapter-ticks` | integer; default 10000; ticks; 1..1000000000 | Simulation ticks per chapter. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--building-catalog` | file | Building catalog manifest. |
| `--compute-threads` | compute; default auto | Shared compute pool; auto uses logical CPU count. |
| `--output-dir` | directory; default . | Translation screenshot directory. |
| `--help` | flag; default false | Show this command's help (also -h). |

Unspecified display, audio and editor settings inherit saved preferences.

Choose at most one: `--fullscreen`, `--no-fullscreen`.

Choose at most one: `--resizable`, `--no-resizable`.

Choose at most one: `--custom-cursor`, `--no-custom-cursor`.

Choose at most one: `--mute`, `--no-mute`.

Choose at most one: `--record`, `--videoshot`.

`--record-chapter-ticks` requires `--record` or `--videoshot`.

`--record-crf` requires `--record` or `--videoshot`.

`--record-encoder` requires `--record` or `--videoshot`.

`--record-fps` requires `--record` or `--videoshot`.

```sh
glob2 dev textshots --output-dir artifacts/textshots
```

### `glob2 dev dump-resources`

Dump map resource diagnostics.

```text
glob2 dev dump-resources FILE [OPTIONS]
```

Outputs: Resource counts on stdout.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 dev dump-resources maps/FourSquares1.map.gz
```

### `glob2 dev dump-wheat`

Dump AI wheat-protection plans.

```text
glob2 dev dump-wheat FILE [OPTIONS]
```

Outputs: Wheat-plan diagnostics on stdout.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--team` | integer; default 0; 0..15 | Team index. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 dev dump-wheat maps/FourSquares1.map.gz --team 0
```

### `glob2 dev dump-tiled`

Dump a repeated map.

```text
glob2 dev dump-tiled FILE [OPTIONS]
```

Outputs: Tiled-map diagnostics on stdout.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--repeat-x` | integer; default 1; 1..32 | Horizontal repetitions. |
| `--repeat-y` | integer; default 1; 1..32 | Vertical repetitions. |
| `--colonies` | integer; default 1; 0..32 | Colonies per tile. |
| `--swarms` | integer; default 0; 0..32 | Swarms per tile. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 dev dump-tiled maps/FourSquares1.map.gz --repeat-x 2 --repeat-y 2
```

### `glob2 dev hive-worker`

Internal Hive JSON-lines worker.

```text
glob2 dev hive-worker [OPTIONS]
```

Outputs: Worker JSON on stdout; accepts stdin; internal subprocess interface.

Platform: native subprocess (browser uses its dedicated worker).

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 dev hive-worker < requests.jsonl
```

### `glob2 info version`

Inspect the executable build.

```text
glob2 info version [OPTIONS]
```

Outputs: Build, SDL, save and network versions.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 info version
```

### `glob2 info sim-version`

Inspect the simulation identity.

```text
glob2 info sim-version [OPTIONS]
```

Outputs: Existing simulation-version JSON or readable fields.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 info sim-version --format json
```

### `glob2 info catalog`

Inspect AI, generator and job capabilities.

```text
glob2 info catalog [OPTIONS]
```

Outputs: Existing headless catalog JSON or readable fields.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--generator-package` | file | Load a custom generator package or directory. Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 info catalog --format json
```

### `glob2 info paths`

Inspect asset search paths.

```text
glob2 info paths [OPTIONS]
```

Outputs: Ordered asset directories.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--data-dir` | directory | Add an asset search directory (in order). Repeatable. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 info paths --data-dir ./data
```

### `glob2 help`

Describe commands without loading game assets.

```text
glob2 help [COMMAND...] [OPTIONS]
```

Outputs: Text help or schema_version=1, cli_version=2 JSON.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--format` | text, json; default text | Output format. |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 help map generate --format json
```

### `glob2 completion`

Print a shell completion script.

```text
glob2 completion SHELL [OPTIONS]
```

Outputs: Bash, Zsh or Fish completion script.

Platform: all.

| Option | Value / default | Meaning |
| --- | --- | --- |
| `--help` | flag; default false | Show this command's help (also -h). |

```sh
glob2 completion bash
```

<!-- END GENERATED CLI REFERENCE -->
