# SPDX-License-Identifier: GPL-3.0-or-later
"""Glass Garden's own mix: the per-set ``mix_fn`` hook of ``score.pipeline.build_trio``.

The maintainer approved this set by ear with this mix chain, which differs from the
shared ``score.mix`` (other level references, a ping-pong echo, a darker bus). Every
stage is loop-aware through the shared ``master`` helpers, so the seam needs no special
handling:

1. **Levels.** Every stem is brought to -20 dBFS by its *active* RMS (the loudest 60 %
   of 100 ms frames) measured on the building mood's version of the same part, so the
   moods keep their composed velocity differences. Then the composition's fader ride
   (``lane(mood, 'expr')``, gain = ride squared) is applied.
2. **Per stem.** The pad gets a light chorus. Each stem is high-passed, set to its
   stereo width (mid/side) and placed with ``score.mix.pan`` as ``MIX`` says, then
   trimmed per mood by ``MOOD_TRIM``.
3. **Space.** The shared synthetic hall (``score.mix.make_ir``, here 2.4 s mid-band
   decay with a longer low end and a shorter top) is fed by the reverb sends through a
   180 Hz high-pass, with 30 % cross-feed, and its return is low-passed at 6.5 kHz. A
   dotted-quaver (0.469 s) ping-pong echo has 0.38 feedback, band-limited to 350 Hz -
   3.2 kHz and darker on every repeat (``echo_ir``). Both use circular convolution.
4. **Bus.** dry + 0.9 x reverb + 0.5 x echo, +1 dB shelf at 110 Hz, -1 dB shelf at
   8 kHz (no "air" lift: it made earlier synth renders sound scratchy), and a gentle
   2:1 compressor (pedalboard).

Loudness and peak limiting are left to ``master.finish``, as for every set. Stateful
stages run through ``master.circular`` with one full loop of padding, so each starts
in exactly the state the end of the loop leaves (the chorus LFO included).
"""
import numpy as np
import scipy.signal as ss

from glob2music import master
from glob2music.score import mix as shared_mix
from glob2music.spec import SAMPLE_RATE as SR

#: part: (balance dB, pan, stereo width, reverb send, echo send, high-pass Hz)
MIX = {
    'lead': (0.0, -0.12, 0.8, 0.30, 0.20, 180),
    'counter': (-5.0, 0.30, 0.7, 0.32, 0.06, 140),
    'pad': (-6.5, 0.0, 1.3, 0.38, 0.0, 110),
    'bass': (-2.5, 0.0, 0.3, 0.04, 0.0, 30),
    'kalimba': (-8.0, 0.0, 1.0, 0.30, 0.18, 150),
    'arp': (-8.0, 0.10, 1.2, 0.18, 0.10, 160),
    'bloops': (-13.0, 0.0, 1.0, 0.45, 0.25, 250),
    'perc': (-3.0, 0.0, 1.0, 0.12, 0.0, 35),
}
#: Extra dB per mood: the lead steps back in calm, the pad and kalimba thin out as the
#: drums arrive, combat leans on lead and bass.
MOOD_TRIM = {'calm': {'lead': -2.0}, 'building': {'pad': -1.5, 'kalimba': -2.0},
             'combat': {'pad': -2.5, 'lead': 0.5, 'bass': 1.0}}
REFERENCE_DB = -20.0
ECHO_S = 0.75 * 60 / 96
ECHO_FEEDBACK = 0.38


def active_rms_db(stem):
    """RMS (dBFS, channel mean) over the loudest 60 % of 100 ms frames."""
    m = stem.mean(axis=1)
    w = int(0.1 * SR)
    frames = np.sqrt((m[:len(m) // w * w].reshape(-1, w) ** 2).mean(axis=1))
    frames = np.sort(frames)[int(len(frames) * 0.4):]
    return 20 * np.log10(np.sqrt((frames ** 2).mean()) + 1e-9)


def echo_ir(seconds=ECHO_S, feedback=ECHO_FEEDBACK, lowpass_hz=3200, highpass_hz=350, taps=14):
    """Impulse response of the approved echo from a mono send. The send reaches the left
    channel ``seconds`` late; left feeds right one delay later, and right feeds left
    *two* delays later, so the repeats fall at 1, 2, 4, 5, 7, 8 ... delays, alternating
    left and right (a lopsided ping-pong that gives the echo its lilt). Each repeat is
    ``feedback`` quieter and passes the first-order low-pass once more (each pass darkens
    it); the send is high-passed once."""
    d = int(round(seconds * SR))
    span = 4096
    lp = ss.butter(1, lowpass_hz, 'low', fs=SR, output='sos')
    h = np.zeros(span)
    h[0] = 1.0
    ir = np.zeros((3 * d * (taps // 2 + 1) + span, 2))
    t, ch, gain = d, 0, 1.0
    for _ in range(taps):
        h = ss.sosfilt(lp, h)
        ir[t:t + span, ch] += gain * h
        t += d if ch == 0 else 2 * d
        ch, gain = 1 - ch, gain * feedback
    return ss.sosfilt(ss.butter(1, highpass_hz, 'high', fs=SR, output='sos'), ir, axis=0)

def mix(folded, composition):
    """``mix_fn`` hook: ``{mood: [(PerformedPart, Instrument, loop)]}`` -> ``{mood: loop}``."""
    import pedalboard
    loop_s = composition.LOOP_SECONDS
    reference = {pp.name: active_rms_db(y) for pp, _, y in folded['building']}
    ir = shared_mix.make_ir(seed=5, rt_low=2.76, rt_mid=2.4, rt_high=1.2, length=3.2, predelay=0.018)
    echo = echo_ir()
    chorus = pedalboard.Pedalboard([pedalboard.Chorus(rate_hz=0.23, depth=0.18, centre_delay_ms=9, feedback=0.0, mix=0.45)])
    glue = pedalboard.Pedalboard([pedalboard.Compressor(threshold_db=-20.0, ratio=2.0, attack_ms=20, release_ms=250)])
    out = {}
    for mood, stems in folded.items():
        n = len(stems[0][2])
        grid = np.arange(0, n + 441, 441)
        ride = composition.lane_at(composition.lane(mood, 'expr'), grid / SR) ** 2
        ride = np.interp(np.arange(n), grid, ride)[:, None]
        dry, rsend, dsend = np.zeros((n, 2)), np.zeros((n, 2)), np.zeros((n, 2))
        for pp, _, y in stems:
            gain, pan, width, rv, dl, hp = MIX[pp.name]
            x = y * 10 ** ((REFERENCE_DB - reference[pp.name]) / 20) * ride
            if pp.name == 'pad':
                x = master.circular(x, lambda z: chorus(z.T.astype(np.float32), SR).T.astype(np.float64), pad_s=loop_s)
            x = master.highpass(x, hp)
            mid, side = (x[:, :1] + x[:, 1:]) * 0.5, (x[:, :1] - x[:, 1:]) * 0.5 * width   # stereo width
            x = shared_mix.pan(np.hstack([mid + side, mid - side]), pan)
            x *= 10 ** ((gain + MOOD_TRIM[mood].get(pp.name, 0.0)) / 20)
            dry += x
            rsend += x * rv
            dsend += x * dl
        rsend = master.highpass(rsend, 180)
        wet = np.stack([master.circular_convolve(rsend[:, 0], ir[:, 0]) * 0.7 + master.circular_convolve(rsend[:, 1], ir[:, 0]) * 0.3,
                        master.circular_convolve(rsend[:, 1], ir[:, 1]) * 0.7 + master.circular_convolve(rsend[:, 0], ir[:, 1]) * 0.3],
                       axis=1)
        wet = master.lowpass(wet, 6500)
        mono = np.repeat(dsend.mean(axis=1, keepdims=True), 2, axis=1)
        bus = dry + 0.9 * wet + 0.5 * master.circular_convolve(mono, echo)
        bus = master.shelf(bus, 110, 1.0, 'low')
        bus = master.shelf(bus, 8000, -1.0, 'high')
        out[mood] = master.circular(bus, lambda z: glue(z.T.astype(np.float32), SR).T.astype(np.float64), pad_s=loop_s)
    return out
