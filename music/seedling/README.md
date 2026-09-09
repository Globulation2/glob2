# Seedling — adaptive soundtrack

Original programmatic composition created for Globulation 2. No existing recordings,
MIDI transcriptions, external samples, or music-generation service outputs are used.

32 bars, 4/4, 96 BPM, 80 seconds, D Dorian. The harmony repeats Dm9 → G6 →
Cmaj9 → Am7, with two bars per chord. A soft synth bed, round bass, woody
call-and-response melody and occasional bells form the shared composition.

- `a1-calm.mid`: core composition.
- `a2-building.mid`: core plus a light arpeggio and percussion.
- `a3-combat.mid`: a separate aggressive arrangement with a distorted bass riff,
  low fifths, brass-like stabs, brief minor-second tension, heavy kick/snare,
  half-time snare, cymbal impacts and tom fills. The opening uses low sustained downbeat growls with answering bursts.
  Playful wood/bell parts are absent.

All files have the same tempo, bar positions and length. MIDI files are type 1,
with named instrument tracks, GM program suggestions, tempo and bar markers.
Open them in a DAW to change notes or assign better instruments. The WAV previews
use simple custom synthesis; their timbres are not an exact GM MIDI rendering.

`generate.py` is the editable composition and preview-rendering source. It needs
Python and NumPy. Run `python3 music/seedling/generate.py` from the repository root.
Regeneration overwrites generated MIDI, WAV and render-info files, so save DAW edits
under separate filenames. Preview release tails and echoes wrap around the loop;
use the same technique when rendering replacement instruments in a DAW.

WAV files are stereo 44.1 kHz / 16-bit and use a fixed shared gain across arrangements;
combat also has gentle output saturation to control peaks. The selected trio is
installed as `data/zik/seedling/a1.ogg`, `a2.ogg`, and `a3.ogg`. Regenerate the game
audio with `python3 music/install_sets.py` after editing the source WAVs.
