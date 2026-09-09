"""Generate Seedling editable MIDI arrangements.

Run: python3 music/seedling/generate.py
Render with music/render.py using the Apple General MIDI sound bank.
"""
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parent
BPM, BARS, PPQ, RATE = 96, 32, 480, 44100
BEAT = 60 / BPM
FRAMES = round(BARS * 4 * BEAT * RATE)
# name, GM program (zero-based), channel, pan
PARTS = [('pad', 89, 0, -.25), ('bass', 38, 1, 0),
         ('wood', 12, 2, -.35), ('bell', 10, 3, .35),
         ('pulse', 80, 4, .15), ('drums', 0, 9, 0),
         ('growl', 30, 5, -.10), ('strike', 61, 6, .20)]
# D Dorian: Dm9, G6, Cmaj9, Am7. Eight-bar harmonic cycle.
CHORDS = [(50, [62, 65, 69, 72, 76]), (43, [62, 64, 67, 69, 71]),
          (48, [60, 64, 67, 71, 74]), (45, [60, 64, 67, 69, 74])]


def compose(level):
    notes = {p[0]: [] for p in PARTS}
    def add(part, beat, pitch, duration, velocity):
        notes[part].append((beat, pitch, duration, velocity))
    for bar in range(BARS):
        root, chord = CHORDS[(bar // 2) % 4]
        start = bar * 4
        if level == 2:
            # Keep D and C in the same subterranean register as the G section.
            root = root - 12 if root >= 48 else root
            # Same harmonic landmarks, but a separate hostile arrangement.
            # Sparse low fifths replace the warm extended chords and bells.
            for pitch in (root, root + 7):
                add('pad', start, pitch, 3.7, 30)
            # Long downbeat growl, space, then a short answering burst.
            # Alternate bars answer with a gallop; four-bar endings descend.
            riff = ([(0, 0, .85), (1.5, 0, .24), (1.875, 0, .10),
                     (2.5, 0, .40), (3.25, 1, .18), (3.5, 0, .30)]
                    if bar % 2 == 0 else
                    [(0, 0, .55), (.75, 0, .18), (1, 0, .18),
                     (1.5, 0, .32), (2.5, 7, .25), (3, 1, .18),
                     (3.5, 0, .30)])
            for j, (offset, interval, length) in enumerate(riff):
                add('growl', start + offset, root - 12 + interval, length,
                    105 if j in (0, 4) else 85)
                if interval == 0:
                    add('bass', start + offset, root - 12, length, 65)
            for offset in (0, 2.5):
                for pitch in (root + 12, root + 19):
                    add('strike', start + offset, pitch, .25, 77)
            if bar % 2:
                # Brief minor-second tension, resolved at the next downbeat.
                for pitch in (root + 12, root + 13):
                    add('strike', start + 3.5, pitch, .2, 66)
            for offset in (0, .375, 1.5, 2.5, 3.5):
                add('drums', start + offset, 36, .28, 115 if offset in (0, 2) else 92)
            for offset in (2,):
                add('drums', start + offset, 38, .25, 110)
            for j in range(8):
                add('drums', start + j * .5, 42, .06, 65 if j % 2 == 0 else 39)
            for offset in (3.25, 3.75):
                add('drums', start + offset, 42, .06, 39)
            if bar % 4 == 0:
                add('drums', start, 49, .7, 80)
            if bar % 4 == 3:
                for j, pitch in enumerate((47, 45, 43)):
                    add('drums', start + 3.25 + j * .25, pitch, .18, 93)
            continue
        for pitch in chord:
            add('pad', start, pitch, 3.8, 35)
        add('bass', start, root - 12, .8 if level == 2 else 1.45, 65)
        add('bass', start + 2.5, root - 12, .4 if level == 2 else .8, 49)
        # Small call-and-response figures leave room for game sounds.
        motif = [0, 2, 1, 4] if bar % 2 == 0 else [3, 2, 1]
        times = [0.5, 1.25, 2.5, 3.25] if bar % 2 == 0 else [.75, 1.5, 3]
        if bar % 8 != 7:
            for j, (offset, degree) in enumerate(zip(times, motif)):
                add('wood', start + offset, chord[degree] + 12, .42, 58 - j * 5)
        if bar % 4 == 0:
            add('bell', start + 3, chord[4] + 12, 1.3, 38)
        if level >= 1:
            for j in range(8):
                add('pulse', start + j * .5, chord[j % 5], .22, 29 + (j % 2) * 7)
            for offset in [.5, 1.5, 2.5, 3.5]:
                add('drums', start + offset, 42, .08, 33)
            for offset in [1, 3]:
                add('drums', start + offset, 37, .12, 44)
    return notes


def vlq(n):
    result = [n & 127]
    while n >> 7:
        n >>= 7
        result.insert(0, (n & 127) | 128)
    return bytes(result)


def chunk(events):
    data, last = bytearray(), 0
    for tick, payload in sorted(events, key=lambda e: e[0]):
        data.extend(vlq(tick - last) + payload)
        last = tick
    data.extend(vlq(BARS * 4 * PPQ - last) + b'\xff\x2f\x00')
    return b'MTrk' + struct.pack('>I', len(data)) + data


def midi(notes, path):
    conductor = [(0, b'\xff\x51\x03' + round(BEAT * 1e6).to_bytes(3, 'big')),
                 (0, b'\xff\x58\x04\x04\x02\x18\x08')]
    for bar in range(BARS):
        label = f'Bar {bar + 1}'.encode()
        conductor.append((bar * 4 * PPQ, b'\xff\x06' + vlq(len(label)) + label))
    tracks = [chunk(conductor)]
    for name, program, channel, pan in PARTS:
        events = [(0, b'\xff\x03' + vlq(len(name)) + name.encode()),
                  (0, bytes([0xC0 | channel, program])),
                  (0, bytes([0xB0 | channel, 10, round(64 + pan * 63)]))]
        for beat, pitch, duration, velocity in notes[name]:
            events.append((round(beat * PPQ), bytes([0x90 | channel, pitch, velocity])))
            events.append((round((beat + duration) * PPQ), bytes([0x80 | channel, pitch, 0])))
        tracks.append(chunk(events))
    path.write_bytes(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ) + b''.join(tracks))


if __name__ == '__main__':
    for level, name in enumerate(('a1-calm', 'a2-building', 'a3-combat')):
        midi(compose(level), OUT / f'{name}.mid')
    print('Seedling MIDI exported')
