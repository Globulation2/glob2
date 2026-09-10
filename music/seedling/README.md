# Seedling

An original 32-bar score in 4/4 at 96 BPM, using D Dorian harmony (Dm9 → G6 → Cmaj9
→ Am7). Calm has pads, bass, wood and bell motifs. Building adds pulse notes and
light percussion. Combat uses low guitar riffs, brass accents and heavy drums.

`generate.py` exports `a1-calm.mid`, `a2-building.mid` and `a3-combat.mid` with named
tracks, GM instruments and bar markers. All three are 80 seconds and position-aligned.
Render with `python3 music/render.py seedling`, then install using `python3
music/install_sets.py seedling`. See `music/README.md` for the shared FluidSynth
workflow. Save DAW edits separately before regenerating MIDI files.
