# Adaptive soundtrack sets

In-game Options lets you choose Original, Seedling, Bramble Dance, Velvet Orbit,
Tidepool, or Random each match. The choice is saved. Switching sets starts the
current mood from the beginning; moods within each set share timing for crossfades.

| Set | Instruments | Loop |
| --- | --- | --- |
| Seedling | Pads, synth bass, wood and bells; guitar and brass combat accents | 80 seconds |
| Bramble Dance | Marimba, bamboo flute, round bass and hand drums in 6/8 | 66.67 seconds |
| Velvet Orbit | Electric piano, glass tones, pads and broken beats | 80 seconds |
| Tidepool | Steel drums, offbeat organ, electric bass and hand percussion | 76.8 seconds |

## MIDI and rendering

The Python generators compose original notes and export editable Type-1 MIDI.
All audio is rendered by FluidSynth 2.6.0 using GeneralUser GS 2.0.3. The bank's
source revision and SHA-256 are pinned in `soundfonts/manifest.json`; its license
is included alongside that manifest. `render.py` is the shared rendering entry
point.

Install FluidSynth 2.6.0, Python 3, NumPy, and libsndfile with Ogg/Vorbis support.
On macOS the Homebrew formula is `fluid-synth`; Linux and Windows use the same
FluidSynth library and SoundFont. Set `FLUIDSYNTH_LIBRARY` to the shared library
path if it is not discovered automatically.
The game plays pre-rendered Ogg files and needs no MIDI synth at runtime.

From the repository root:

```sh
python3 music/seedling/generate.py
python3 music/contrasts/generate.py
python3 music/tidepool/generate.py
python3 music/fetch_soundfont.py
python3 music/render.py
python3 music/install_sets.py
```

Pass set IDs to render or install only selected sets, for example
`python3 music/render.py tidepool`. Pass `--sound-bank /path/to/bank.sf2` to audition
another compatible premade bank. MIDI program numbers and pan settings select each instrument;
percussion uses the GM drum bank on channel 10. Program changes must be at tick zero.
Save DAW edits separately before regenerating MIDI files.

The renderer uses 44.1 kHz stereo output, one synthesis thread, fourth-order
interpolation, fixed reverb settings and disabled chorus. Two warm-up loops precede
the captured loop. `mix.json` sets each arrangement's RMS balance, followed by a
shared peak gain for its trio. MIDI's integer tempo precision determines frame counts.
These settings and the bank checksum are recorded in `render-info.json`. The pinned
version and bank control rendering inputs; cross-platform floating-point calculations
may still differ slightly. Shipping the same Ogg assets avoids platform-specific
instrument differences during gameplay.

A preview cycles calm → building → combat → calm, with transition times recorded
in `render-info.json`. Generated WAVs and downloaded sound banks are ignored by Git.

`installed-sets.json` records the decoded Ogg frame counts and peaks. Different
sets have independent tempos; only moods within a set are position-aligned.

## Checks

```sh
python3 -m unittest discover -s music -p 'test_*.py'
scons --build=build-validation release=1 -j16 music-set-tests
python3 test/run-music-set-tests.py
```

The game harness uses a disposable profile and dummy audio, and needs a graphical
display to construct Options. It checks discovery, decoder rollback, mood changes,
selector actions, mute state and saved preferences.
