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
All audio is rendered by Apple's premade AVAudioUnitSampler using the macOS General
MIDI DLS sound bank. `render.py` is the shared rendering entry point; there are no
hand-written instrument oscillators or drum synthesizers. NumPy only handles PCM
validation, gain and preview crossfades. The system sound bank is not bundled.

Rendering requires macOS, Xcode command-line tools, Python 3 and NumPy. Installation
also requires libsndfile with Ogg/Vorbis support. The game plays the resulting Oggs
on all supported platforms and does not need Apple's audio framework at runtime.

From the repository root:

```sh
python3 music/seedling/generate.py
python3 music/contrasts/generate.py
python3 music/tidepool/generate.py
python3 music/render.py
python3 music/install_sets.py
```

Pass set IDs to render or install only selected sets, e.g. `python3 music/render.py
tidepool`. Pass `--sound-bank /path/to/bank.sf2` to audition another compatible
premade bank. MIDI program numbers and pan settings select each instrument;
percussion uses the GM drum bank on channel 10. Program changes must be at tick zero.
Save DAW edits separately before regenerating MIDI files.

The renderer runs two warm-up loops and captures the third for release continuity.
It renders stereo 44.1 kHz audio and applies one shared gain per trio, preserving
relative dynamics. A preview cycles calm → building → combat → calm; transition
times are recorded in `render-info.json`. MIDI's integer tempo precision determines
the exact frame count. WAV previews and compiled renderers are not committed.

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
