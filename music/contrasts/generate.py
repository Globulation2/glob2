"""Editable MIDI scores for Bramble Dance and Velvet Orbit.

Run python3 music/contrasts/generate.py. Render the MIDI files with music/render.py.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from midi_tools import midi

ROOT = Path(__file__).resolve().parent
SR, PPQ = 44100, 480
STATES = ('calm', 'building', 'combat')
SETS = {
 'bramble-dance': dict(bpm=108, bars=40, beats=3, meter=(6, 3),
   parts=[('marimba',12,0,-.3),('bamboo',75,1,.25),('low-marimba',12,2,.1),('round-bass',38,3,0),('hand',0,9,0)],
   description='Wooden marimba and bamboo flute in 6/8, with round bass and hand drums.'),
 'velvet-orbit': dict(bpm=72, bars=24, beats=4, meter=(4, 2),
   parts=[('piano',4,0,-.15),('glass',98,1,.35),('air',91,2,-.2),('sub',38,3,0),('breaks',0,9,0)],
   description='Slow, spacious electric-piano harmony and glass tones; combat is a heavy broken-beat arrangement.')}



def compose(key, c, level):
    notes = {p[0]: [] for p in c['parts']}
    def add(part, b, p, d, v): notes[part].append((b,p,d,v))
    for bar in range(c['bars']):
        b = bar * c['beats']
        if key == 'bramble-dance':
            root, chord = [(38,[62,66,69]),(43,[62,67,71]),(47,[62,66,71]),(45,[61,64,69])][(bar//2)%4]
            # Six eighth-notes, grouped 3+3.
            pattern = [0,1,2,1,2,1] if bar%2==0 else [2,1,0,1,2,0]
            for j,k in enumerate(pattern):
                add('marimba',b+j*.5,chord[k],.43,65 if j%3==0 else 49)
            for off in (0,1.5): add('round-bass',b+off,root,1.2,65+level*9)
            melody = ([74,76,78,81,78,76] if bar%2==0 else [78,76,74,73,74,69])
            # Four-bar melodic answers are transposed down rather than repeated.
            shift = -5 if (bar//4)%2 else 0
            if bar%8 != 7:
                for j,p in enumerate(melody):
                    if level<2 and j in (0,2,3,5): add('bamboo',b+j*.5,p+shift,.42,51)
                    if level==2: add('low-marimba',b+j*.5,p+shift-12,.36,84)
            if level>=1:
                for off,p in ((0,41),(1,60),(1.5,41),(2.5,62)):
                    add('hand',b+off,p,.22,65+level*12)
                for j in range(6): add('hand',b+j*.5,70,.08,29+level*8)
            if level==2:
                for off in (0,.75,1.5,2.25): add('hand',b+off,36,.25,105)
                # Low mallet accents support the combat melody.
                for off in (0,1.5):
                    add('marimba',b+off,root+12,.24,85)
        elif key == 'velvet-orbit':
            root, chord = [(39,[58,62,65,69]),(36,[58,62,63,67]),(44,[55,60,63,67]),(46,[56,60,62,65])][(bar//2)%4]
            # Long jazz voicings and deliberate empty space.
            for off in ((0,2.75) if bar%2==0 else (1.5,)):
                for j,p in enumerate(chord): add('piano',b+off+j*.015,p,.95,57-level*5)
            for off,p in ((0,root-12),(2.5,root-12 if bar%2==0 else root-5)):
                add('sub',b+off,p,.8 if level==2 else 1.35,70+level*10)
            if bar%2==0:
                for off,p in ((.75,chord[3]+12),(2.25,chord[1]+12)):
                    add('glass',b+off,p,.8,42 if level<2 else 29)
            if bar%2==0:
                for p in chord[::2]: add('air',b,p+12,7.5,24)
            if level>=1:
                for off in (0,2.5): add('breaks',b+off,36,.26,68+level*12)
                for off in (1,3): add('breaks',b+off,38,.23,57+level*17)
                # Laid-back swung hats, against the straight mallet pattern.
                for off in (0,.66,1,1.66,2,2.66,3,3.66): add('breaks',b+off,42,.07,32+level*5)
            if level==2:
                for off in (.75,1.75,2.25,3.5): add('breaks',b+off,36,.22,101)
                for off in (1.75,3.75): add('breaks',b+off,38,.12,53)
                for off in (1.5,3.5): add('sub',b+off,root-12,.3,102)
    return notes


if __name__ == '__main__':
    for key, config in SETS.items():
        folder = ROOT / key
        folder.mkdir(exist_ok=True)
        for level, state in enumerate(STATES):
            midi(config, compose(key, config, level), folder / f'{state}.mid')
        print(key, 'MIDI exported')
