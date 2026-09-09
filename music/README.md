# Adaptive soundtrack sets

The in-game Options menu lets you cycle through Original, Seedling, Bramble Dance,
Velvet Orbit and Tidepool, or choose **Random each match**. The choice is saved.
Selecting a different set starts the current mood from the beginning. Within a
set, calm, building and combat share timing and harmony for the game's existing
mood crossfades.

| Set | Character | Loop |
| --- | --- | --- |
| Seedling | Organic electronic, woody melody, heavy combat bass | 80 seconds |
| Bramble Dance | Marimba, bamboo flute and hand drums in 6/8 | 66.67 seconds |
| Velvet Orbit | Spacious electric piano and broken beats | 80 seconds |
| Tidepool | Caribbean-inspired steelpan, offbeat organ and syncopated bass | 76.8 seconds |

The four new sets are original programmatic compositions with custom synthesis;
no external samples or existing recordings are used. Each includes editable
Type-1 MIDI originals and its Python composition/rendering source. MIDI instrument
assignments are GM suggestions; a DAW's instruments will sound different from the
custom renders. Save DAW edits separately before regenerating.

## Regeneration

Requires Python 3, NumPy, and libsndfile with Ogg/Vorbis support. From the repository
root, run:

```sh
python3 music/seedling/generate.py
python3 music/contrasts/generate.py
python3 music/tidepool/generate.py
python3 music/install_sets.py
```

The generators create stereo 44.1 kHz WAV loops and previews. The installer encodes
and verifies the runtime Ogg trios in `data/zik/`, recording decoded lengths and
peaks in `installed-sets.json`. To install one regenerated set, pass its directory
name, for example `python3 music/install_sets.py tidepool`.

Runtime Oggs, MIDI originals and source are tracked. Generated WAV previews and
ZIP bundles are ignored. Release tails wrap around loop boundaries. Different
sets have different tempos and lengths, so only moods within a set are aligned.

## Integration checks

```sh
scons --build=build-validation release=1 -j16 music-set-tests
python3 test/run-music-set-tests.py
```

The harness uses a disposable profile and SDL dummy audio, and requires a graphical
display to construct the Options screen. It checks set discovery, decoder rollback,
mood preservation, selector behavior, mute state and saved preferences.
