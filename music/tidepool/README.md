# Tidepool

An original Caribbean-inspired adaptive soundtrack for Globulation 2: a light
steelpan-like hook, woody offbeat organ chords, a melodic syncopated bass line,
shakers and conga-style hand percussion. D minor → B-flat → F → C triads support
a repeating two-bar question/answer hook. All lead and bass notes belong to the
current chord, with space around the melody and restrained percussion fills.

Calm leaves room around the groove. Building adds interlocking organ figures and
hand percussion. Combat keeps the same hook and harmonic timeline, with lower
wooden doubles, deeper bass, stronger drums and short fills.

All three tracks are 32 bars in 4/4 at 100 BPM, exactly 76.8 seconds, stereo
44.1 kHz/16-bit. The generated 38.4-second demo crossfades to building at 9.6 seconds,
combat at 19.2 seconds, and calm at 28.8 seconds without resetting playback position.

The MIDI originals contain separate named instrument tracks with GM suggestions.
The WAV timbres are custom synthesis, so another MIDI sound bank will sound
different. The composition and synth are in `generate.py`; it imports only the
MIDI/WAV writers from `../contrasts/generate.py`. Requires Python and NumPy.

Regenerate with `python3 music/tidepool/generate.py`, then encode/install with
`python3 music/install_sets.py tidepool`. Save DAW edits separately because
regeneration overwrites generated files. Oggs are installed in `data/zik/tidepool/`.
