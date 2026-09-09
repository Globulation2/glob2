"""Tidepool: Caribbean-inspired Glob music with MIDI originals.

Run python3 music/tidepool/generate.py to export MIDI.
Render the MIDI files with music/render.py.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import midi_tools as w
ROOT = Path(__file__).resolve().parent
BPM = 100
CFG=dict(bpm=BPM,bars=32,beats=4,meter=(4,2),parts=[
 ('pan',114,0,-.25),('chop',16,1,.30),('bubble',16,2,-.35),
 ('bass',33,3,0),('wood',12,4,.20),('air',89,5,0),('percussion',0,9,0)])
CHORDS=[(38,[62,65,69]),(38,[62,65,69]),(46,[62,65,70]),(46,[62,65,70]),
        (41,[60,65,69]),(41,[60,65,69]),(36,[60,64,67]),(36,[60,64,67])]
MELODY=[
 [(.5,69,.18),(.75,74,.28),(1.5,77,.45),(2.75,74,.60)],
 [(.5,77,.25),(1,74,.35),(2,69,.25),(2.75,74,.80)],
 [(.5,70,.18),(.75,74,.28),(1.5,77,.45),(2.75,74,.60)],
 [(.5,77,.25),(1,74,.35),(2,70,.25),(2.75,74,.80)],
 [(.5,69,.18),(.75,72,.28),(1.5,77,.45),(2.75,72,.60)],
 [(.5,77,.25),(1,72,.35),(2,69,.25),(2.75,72,.80)],
 [(.5,67,.18),(.75,72,.28),(1.5,76,.45),(2.75,72,.60)],
 [(.5,76,.25),(1,72,.35),(2,67,.25),(2.75,72,.80)]]


def building_arrangement():
    notes = {part[0]: [] for part in CFG['parts']}

    def add(part, beat, pitch, duration, velocity):
        notes[part].append((beat, pitch, duration, velocity))

    for bar in range(32):
        beat = bar * 4
        root, chord = CHORDS[bar % 8]
        for j, (offset, pitch, duration) in enumerate(MELODY[bar % 8]):
            add('pan', beat + offset, pitch, duration, 67 + j % 2 * 7)
        for offset in (.5, 1.5, 2.5, 3.5):
            for j, pitch in enumerate(chord):
                add('chop', beat + offset + j * .012, pitch, .13, 54)
        for offset, degree in ((.75, 0), (2.75, 2)):
            add('bubble', beat + offset, chord[degree] - 12, .12, 41)
        for offset, interval, duration in ((0, 0, .60), (1.5, 7, .32), (2.5, 0, .65), (3.5, 7, .30)):
            add('bass', beat + offset, root + interval, duration, 88)
        if bar % 2 == 0:
            for pitch in chord[::2]:
                add('air', beat + .25, pitch, 6.9, 15)
        add('percussion', beat + 2, 36, .28, 79)
        add('percussion', beat + 2, 37, .18, 75)
        for offset in (0, .5, 1, 1.5, 2, 2.5, 3, 3.5):
            add('percussion', beat + offset, 70, .07, 31 + int(offset) % 2 * 5)
        for offset, pitch in ((.75, 64), (2.75, 62)):
            add('percussion', beat + offset, pitch, .18, 52)
    return notes


def compose(level):
    if level == 1:
        return building_arrangement()
    notes={p[0]:[] for p in CFG['parts']}
    def add(part,b,p,d,v):notes[part].append((b,p,d,v))
    for bar in range(32):
        b=bar*4;k=bar%8;root,chord=CHORDS[k]
        if level == 0:
            # The opening half of each phrase carries the hook; its answer breathes.
            for j,(off,p,d) in enumerate(MELODY[k]):
                if bar%2 == 0 or j in (0,3):
                    add('pan',b+off,p,d*1.15,52+j%2*5)
            for off in (1.5,3.5):
                for j,p in enumerate(chord):add('chop',b+off+j*.012,p,.13,27)
            add('bass',b,root,1.5,60)
            add('bass',b+2.5,root,.85,53)
            if k%2==0:
                for p in chord[::2]:add('air',b+.25,p,6.9,19)
            for off in (.5,1.5,2.5,3.5):add('percussion',b+off,70,.07,18)
            if bar%2==1:add('percussion',b+2,37,.12,27)
        else:
            # The hook moves down an octave, with dry wooden attack on every note.
            for j,(off,p,d) in enumerate(MELODY[k]):
                add('pan',b+off,p-12,d*.8,83+j%2*7)
                add('wood',b+off,p-12,d*.65,112)
            for off in (.5,2.5,3.5):
                for j,p in enumerate(chord):add('chop',b+off+j*.012,p,.10,82)
            for off,interval,d in ((0,0,.38),(.75,0,.24),(1.5,7,.30),(2,0,.38),(2.75,0,.24),(3.5,7,.30)):
                add('bass',b+off,root+interval,d,113 if interval==0 else 98)
            for off in (0,.75,2,2.75):add('percussion',b+off,36,.24,120 if off in (0,2) else 98)
            for off in (1,3):add('percussion',b+off,38,.20,112)
            for off,p in ((.5,64),(1.75,62),(2.5,64),(3.75,62)):
                add('percussion',b+off,p,.16,79)
            for j in range(8):add('percussion',b+j*.5,42,.07,58 if j%2 else 38)
            if k in (3,7):
                for off,p in ((3.25,47),(3.5,45),(3.75,41)):
                    add('percussion',b+off,p,.15,95)
    return notes


if __name__ == '__main__':
    for level, name in enumerate(('calm', 'building', 'combat')):
        w.midi(CFG, compose(level), ROOT / f'{name}.mid')
    print('Tidepool MIDI exported')
