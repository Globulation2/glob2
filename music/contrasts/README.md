# Contrasts — two independent adaptive scores

Each set has its own meter, tempo, instruments, harmony and groove. These are
original programmatic compositions with custom synthesis and editable Type-1 MIDI.

| Set | Tempo/meter | Character |
| --- | --- | --- |
| Bramble Dance | 108 quarter-note BPM, 6/8 (72 dotted-quarter beats/minute) | Bright plucked-string folk dance and flute; combat brings bowed-string runs and pounding hand drums. |
| Velvet Orbit | 72 BPM, 4/4 | Spacious electric-piano extended chords, glass tones and swung percussion; combat becomes a heavy broken beat with saturated sub-bass. |

Within each set, calm/building/combat have exactly the same tempo, harmonic
timeline, meter and audio frame count. Different sets are separate soundtrack
choices and should not be crossfaded together at arbitrary playback positions.

Each folder contains three MIDI originals and render statistics. Regeneration
creates three full WAV loops and a demo. The 36-second demo crossfades into building at 9 seconds,
combat at 18 seconds, and calm at 27 seconds, preserving playback position.
These offline demos use one-second crossfades.

Run `python3 music/contrasts/generate.py` with NumPy installed to regenerate all
assets. The script is self-contained: musical material, MIDI export, instrument
synthesis and demo arrangement are editable in one file. Regeneration overwrites
generated files, so save DAW edits separately. MIDI GM program numbers are timbre
suggestions; MIDI playback through a DAW sound bank will differ from the previews.

Full loops are stereo 44.1 kHz/16-bit, with release and echo tails wrapped at the
loop boundary. Bramble Dance is about 66.67 seconds, Velvet Orbit is 80 seconds. Sub-sample rounding applies at loop ends;
MIDI tempo values also have the standard integer-microsecond precision.
