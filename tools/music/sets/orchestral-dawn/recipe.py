# SPDX-License-Identifier: GPL-3.0-or-later
"""Orchestral Dawn: one ACE-Step 1.5 generation, split by Demucs into mood layers.

AI-generated (disclosed in set.toml). The model wrote a 110 s "full" piece, with drums,
from the prompt below. Demucs (htdemucs_6s) estimates its drums and bass, and the
three moods are layer subsets of that one recording, so they share a timeline:

* calm     = melodic + bass at -8 dB             (no drums)
* building = melodic + bass at -3 dB + drums at -9 dB, high-passed at 300 Hz
             (the kick is removed and the frame drum and woodblock stay)
* combat   = melodic + bass + drums              (the generated mix itself)

where melodic = mix - drums - bass. The separator's residual therefore stays in the
melodic layer, and combat is the untouched generation before mastering.

The loop is 28 bars (70.751 s) of the body. The generation has an intro up to
about 10 s and stops at about 105 s, and these are excluded. Its last 4 beats are
crossfaded into the 4 beats that precede the loop start in the source
(``loop.crossfade_loop``), so the wrap continues into bar 1 with natural tails.

Reproducibility: the generation is cached by ``genai.acestep`` and checked against
``GENERATION_SHA256``. Regenerating does NOT give the same music, even with the same
seed on the same GPU (tested: chroma correlation 0.31 with the original), and other
hardware differs further, so the loop points and gains below only fit the approved
WAV. Maintainers hold that WAV locally (it is not committed); to rebuild the shipped
mix, place it at ``acestep.cache_path(GENERATION, ctx.cache_dir)``. A different file
is an error unless the build passes ``--allow-regenerate``, which accepts a new,
unreviewed piece.
"""
from glob2music import master
from glob2music.adapt import demucs, stems
from glob2music.audio import Trio, db_to_gain
from glob2music.genai import acestep
from glob2music.loop import crossfade_loop

#: The exact request of the approved generation (seed 11).
#: ACE-Step's LM planner kept the given metadata (96 bpm, G minor, 4/4, 110 s);
#: the model actually played at about 95.7 bpm.
GENERATION = acestep.Request(
    caption=('Playful whimsical orchestral instrumental for a cute strategy game about little blob '
             'creatures. Bouncy pizzicato strings and bassoon carry a quirky melody, clarinet and flute '
             'answer, glockenspiel sparkles, warm upright bass, frame drum and woodblock groove with '
             'timpani accents. Organic acoustic chamber orchestra, storybook adventure, lively but warm.'),
    bpm=96, keyscale='G minor', timesignature='4', duration=110, seed=11,
)
#: SHA-256 of the approved generation (48 kHz 16-bit WAV, 15,360,078 bytes, made on an
#: RTX 2070 SUPER: turbo DiT in float16, LM in bfloat16). Not committed (15 MB).
GENERATION_SHA256 = '7fd51ef7a6bdd6d566bd3c0ff3fd879ba144d3b5f25f9d669b1f79850ea69e32'

#: Loop frames at 48 kHz: 24.056 s .. 94.807 s, two tracked beats 112 beats (28 bars)
#: apart. The window was chosen by a loop search that compared the bar before each
#: end with the bar before each start (chroma, MFCC, level), after skipping the intro
#: and the ending.
LOOP_START, LOOP_END = round(1060870 * 48000 / 44100), round(4180989 * 48000 / 44100)
#: Seam crossfade: 4 beats at 95.7 bpm.
CROSSFADE = round(110603 * 48000 / 44100)          # 2.508 s

#: Mood gains in dB per layer (None = layer absent), and the building drums' high-pass.
MOODS = {
    'calm': dict(melodic=0.0, bass=-8.0, drums=None),
    'building': dict(melodic=0.0, bass=-3.0, drums=-9.0),
    'combat': dict(melodic=0.0, bass=0.0, drums=0.0),
}
BUILDING_DRUMS_HIGHPASS_HZ = 300.0


def build(ctx):
    wav = acestep.generate(GENERATION, ctx.cache_dir, expected_sha256=GENERATION_SHA256, logger=ctx.log,
                           allow_mismatch=ctx.allow_regenerate)
    # stems.decode resamples 48 kHz -> 48 kHz with ffmpeg; Demucs separates that same
    # array, so mix and stems share frames exactly. overlap=0.5 matches the approved build.
    mix = stems.decode(wav)
    parts = demucs.separate(mix, 'htdemucs_6s', cache_dir=ctx.work_dir / 'demucs', overlap=0.5)
    layers = {'drums': parts['drums'], 'bass': parts['bass']}
    layers['melodic'] = mix - layers['drums'] - layers['bass']      # guitar, piano, other, vocals, residual
    # Cut every layer at the same frames, so the moods stay sample-aligned.
    loops = {name: crossfade_loop(y, LOOP_START, LOOP_END, CROSSFADE) for name, y in layers.items()}
    # Loop-aware (circular) zero-phase high-pass, as in the approved build.
    loops['drums_hp'] = master.highpass(loops['drums'], BUILDING_DRUMS_HIGHPASS_HZ, zero_phase=True)
    moods = {}
    for mood, gains in MOODS.items():
        y = 0.0
        for name, g in gains.items():
            if g is None:
                continue
            src = loops['drums_hp'] if (mood == 'building' and name == 'drums') else loops[name]
            y = y + src * db_to_gain(g)
        moods[mood] = y
    return Trio(**moods, meta={'generation': GENERATION.key(), 'loop_frames': [LOOP_START, LOOP_END],
                               'separation': 'htdemucs_6s shifts=2 overlap=0.5'})
