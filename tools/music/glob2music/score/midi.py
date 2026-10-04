# SPDX-License-Identifier: GPL-3.0-or-later
"""MIDI export: renderable performances and a readable score.

Pipeline role: two kinds of file come out of the score layer.

* ``write_performance`` writes one ``PerformedPart`` as a single-track SMF on a fixed
  120 bpm clock (1920 ticks per second), so event times are plain seconds and no
  renderer can misread a tempo map. A pre-roll shifts everything later so notes
  humanised to start before beat 0 survive, and the end-of-track marker leaves room
  for release and reverb tails. This is what ``backends/sfizz.py`` renders; a
  synthesiser backend can read the same file or the ``PerformedPart`` directly.
* ``write_score`` writes the *written* notes of every part of every mood, quantised,
  with the real tempo map and time signature: the file to open in a notation program
  or DAW to read or edit the arrangement.

``read_notes`` reads a performance file back as ``(on_s, off_s, pitch)`` triples (with
the pre-roll removed), for the dropout scan in ``checks.py``.
"""
import mido

#: Ticks per beat of performance files; at 120 bpm that is 1920 ticks per second.
TPQ = 960
TICKS_PER_SECOND = 1920


def write_performance(path, part, loop_seconds, preroll_s, tail_s):
    """Write ``part`` (a ``PerformedPart``) for rendering.

    Messages at the same instant are ordered CC, note-off, note-on, so a repeated key
    is released before it is struck again. Without a CC11 curve, CC11 is set to 127.
    """
    mid = mido.MidiFile(ticks_per_beat=TPQ)
    tr = mido.MidiTrack()
    mid.tracks.append(tr)
    tr.append(mido.MetaMessage('set_tempo', tempo=500000, time=0))
    msgs = [(t + preroll_s, 0, mido.Message('control_change', control=11, value=v)) for t, v in part.cc11]
    if not part.cc11:
        msgs.append((0.0, 0, mido.Message('control_change', control=11, value=127)))
    for on, off, p, vel in part.events:
        msgs.append((on + preroll_s, 2, mido.Message('note_on', note=p, velocity=vel)))
        msgs.append((off + preroll_s, 1, mido.Message('note_off', note=p, velocity=0)))
    msgs.sort(key=lambda m: (m[0], m[1]))
    last = 0
    for t, _, m in msgs:
        tick = max(0, int(round(t * TICKS_PER_SECOND)))
        m.time = max(0, tick - last)
        last = max(last, tick)
        tr.append(m)
    end_tick = int(round((preroll_s + loop_seconds + tail_s) * TICKS_PER_SECOND))
    tr.append(mido.MetaMessage('end_of_track', time=max(0, end_tick - last)))
    mid.save(str(path))
    return path


def read_notes(path, preroll_s=0.0):
    """``[(on_s, off_s, pitch)]`` of a performance file, pre-roll removed."""
    t, pending, out = 0, {}, []
    for m in mido.MidiFile(str(path)).tracks[0]:
        t += m.time
        if m.type == 'note_on' and m.velocity > 0:
            pending.setdefault(m.note, []).append(t / TICKS_PER_SECOND)
        elif m.type in ('note_off', 'note_on') and pending.get(m.note):
            out.append((pending[m.note].pop(0) - preroll_s, t / TICKS_PER_SECOND - preroll_s, m.note))
    return sorted(out)


def write_score(path, score, parts_by_mood):
    """Readable multi-track MIDI of the written arrangement (one track per part and
    mood, named ``mood:part (instrument)``), with tempo map and time signature."""
    tpq = 480
    mid = mido.MidiFile(ticks_per_beat=tpq)
    t0 = mido.MidiTrack()
    mid.tracks.append(t0)
    t0.append(mido.MetaMessage('track_name', name=score.title))
    num, den = score.time_signature
    t0.append(mido.MetaMessage('time_signature', numerator=num, denominator=den))
    last = 0
    spb = score.tempo.spb
    for b, s in enumerate(spb):
        if b == 0 or abs(s - spb[b - 1]) > 1e-9:
            t0.append(mido.MetaMessage('set_tempo', tempo=int(s * 1e6), time=b * tpq - last))
            last = b * tpq
    for mood, parts in parts_by_mood.items():
        for ch, part in enumerate(parts):
            tr = mido.MidiTrack()
            mid.tracks.append(tr)
            tr.append(mido.MetaMessage('track_name', name=f'{mood}:{part.name} ({part.instrument})'))
            msgs = []
            for n in part.notes:
                for p in n.pitches:
                    msgs.append((int(round(n.start * tpq)), 1, mido.Message('note_on', note=p, velocity=80, channel=ch % 16)))
                    msgs.append((int(round(n.end * tpq)) - 1, 0, mido.Message('note_off', note=p, velocity=0, channel=ch % 16)))
            msgs.sort(key=lambda m: (m[0], m[1]))
            lt = 0
            for t, _, m in msgs:
                m.time = t - lt
                lt = t
                tr.append(m)
    mid.save(str(path))
    return path
