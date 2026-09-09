"""Tidepool: Caribbean-inspired Glob music with MIDI originals. Requires NumPy.

Run python3 music/tidepool/generate.py. Uses the shared MIDI/WAV writers only;
the composition and synthesis below are specific to this soundtrack.
"""
from pathlib import Path
import importlib.util
import json
import zipfile
import numpy as np

ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('writers',ROOT.parent/'contrasts/generate.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
SR=44100;BPM=100;BEAT=60/BPM;FRAMES=round(128*BEAT*SR)
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


def compose(level):
    notes={p[0]:[] for p in CFG['parts']}
    def add(part,b,p,d,v):notes[part].append((b,p,d,v))
    for bar in range(32):
        b=bar*4;k=bar%8;root,chord=CHORDS[k]
        melody=MELODY[k]
        for j,(off,p,d) in enumerate(melody):
            # Battle retains the hook, doubled by lower wood rather than guitars.
            add('pan',b+off,p,d,60+j%2*7+level*7)
            if level==2 and j%2==0:add('wood',b+off,p-12,d*.8,88)
        # Short offbeat chords and a little anticipatory last-sixteenth answer.
        for off in (.5,1.5,2.5,3.5):
            for j,p in enumerate(chord[:3]):
                add('chop',b+off+j*.012,p,.13,42+level*12)
        if level>=1:
            for off,degree in ((.75,0),(2.75,2)):
                add('bubble',b+off,chord[degree]-12,.12,36+level*5)
        # One recurring root/fifth bass figure anchors the hook in every bar.
        riff=[(0,0,.60),(1.5,7,.32),(2.5,0,.65),(3.5,7,.30)]
        for off,interval,d in riff:add('bass',b+off,root+interval,d,76+level*12)
        if k%2==0:
            for p in chord[::2]:add('air',b+.25,p,6.9,15 if level<2 else 9)
        # One-drop center of gravity; congas supply the movement around it.
        add('percussion',b+2,36,.28,57+level*22)
        add('percussion',b+2,37 if level<2 else 38,.18,52+level*23)
        for off in (0,.5,1,1.5,2,2.5,3,3.5):
            add('percussion',b+off,70,.07,24+(int(off)%2)*5+level*7)
        if level>=1:
            for off,p in ((.75,64),(2.75,62)):
                add('percussion',b+off,p,.18,42+level*10)
        if level==2:
            for off in (0,.75,1.5,2.75,3.5):add('percussion',b+off,36,.22,100 if off==0 else 84)
            for off in (1.75,3.75):add('percussion',b+off,38,.10,46)
            for off in (.5,1.5,2.5,3.5):add('percussion',b+off,42,.07,49)
            if k==7:
                for j,p in enumerate((45,41)):add('percussion',b+3.25+j*.5,p,.18,70)
    return notes


def voice(part,p,d,level,rng):
    tail=.8 if part in ('pan','air') else .18
    t=np.arange(round((d+tail)*SR))/SR;f=440*2**((p-69)/12);ph=2*np.pi*f*t
    gate=np.exp(-np.maximum(t-d,0)*32);attack=1-np.exp(-t*550)
    if part=='pan':
        # Tuned shell partials: a soft steelpan suggestion rather than a loud bell.
        y=np.sin(ph)*np.exp(-t*3.5)+.44*np.sin(ph*2.006)*np.exp(-t*7)
        y+=.16*np.sin(ph*3.99)*np.exp(-t*14)+.08*np.sin(ph*2.73)*np.exp(-t*22)
        env=attack*gate;gain=.21
    elif part in ('chop','bubble'):
        y=np.sin(ph)+.38*np.sin(ph*2)+.18*np.sin(ph*3)+.08*np.sin(ph*4)
        env=(1-np.exp(-t*240))*np.exp(-t*13)*gate;gain=.17 if part=='chop' else .16
    elif part=='bass':
        y=np.sin(ph)+.24*np.sin(ph*2)*np.exp(-t*5)+.12*np.sin(ph*3)*np.exp(-t*9)
        if level==2:y=.70*np.tanh(2*y)+.26*np.sin(ph*.5)
        env=(1-np.exp(-t*110))*np.exp(-t*1.4)*gate;gain=.36
    elif part=='wood':
        y=np.sin(ph)+.23*np.sin(ph*2.76)*np.exp(-t*8)
        env=attack*np.exp(-t*9)*gate;gain=.19
    elif part=='air':
        y=np.sin(ph)+.22*np.sin(ph*1.003)
        env=np.minimum(t/.4,1)*np.exp(-np.maximum(t-d,0)*5);gain=.10
    else:
        noise=rng.uniform(-1,1,len(t))
        if p==36:
            y=np.sin(2*np.pi*(48*t+2.5*(1-np.exp(-t*42))))
            if level==2:y=np.tanh(1.6*y)
            env=(1-np.exp(-t*1400))*np.exp(-t*15)
        elif p in (37,38):
            y=(.40*noise+.5*np.sin(2*np.pi*420*t)) if p==37 else (.7*noise+.35*np.sin(2*np.pi*180*t))
            env=(1-np.exp(-t*1500))*np.exp(-t*(60 if p==37 else 23))
        elif p in (70,42):
            y=np.r_[0,np.diff(noise)]*.5
            env=(1-np.exp(-t*1400))*np.exp(-t*85)
        elif p==75:
            y=np.sin(2*np.pi*820*t)+.4*np.sin(2*np.pi*1250*t)
            env=(1-np.exp(-t*1200))*np.exp(-t*60)
        else:
            drum_f={64:175,62:270,63:235,47:140,45:110,41:80}[p]
            y=np.sin(2*np.pi*drum_f*t)+.32*np.sin(2*np.pi*drum_f*1.59*t)+.10*noise*np.exp(-t*60)
            env=(1-np.exp(-t*1000))*np.exp(-t*21)
        gain=.32
    return y*env*gain


def render(notes,level):
    a=np.zeros((FRAMES,2),dtype=np.float32);rng=np.random.default_rng(709)
    for part,_,_,pan in CFG['parts']:
        for b,p,d,v in notes[part]:
            y=voice(part,p,d*BEAT,level,rng)*v/127
            idx=(round(b*BEAT*SR)+np.arange(len(y)))%FRAMES
            a[idx,0]+=y*np.sqrt((1-pan)/2);a[idx,1]+=y*np.sqrt((1+pan)/2)
    a+=.09*np.roll(a[:,::-1],round(.75*BEAT*SR),axis=0)
    a*=(.14,.18,.25)[level]/float(np.sqrt(np.mean(a*a)))
    return .94*np.tanh(a/.94)


if __name__=='__main__':
    mixes=[];report={'bpm':BPM,'bars':32,'seconds':FRAMES/SR,'tracks':{}}
    for level,name in enumerate(('calm','building','combat')):
        notes=compose(level);w.midi(CFG,notes,ROOT/f'{name}.mid')
        a=render(notes,level);w.wav(ROOT/f'{name}.wav',a);mixes.append(a)
        report['tracks'][name]={'frames':len(a),'peak':float(np.max(np.abs(a))),
            'rms':float(np.sqrt(np.mean(a*a))),'boundary_step':float(np.max(np.abs(a[0]-a[-1])))}
    span=round(16*BEAT*SR);n=4*span;demo=mixes[0][:n].copy()
    for sec,src,dst in ((1,0,1),(2,1,2),(3,2,0)):
        start=sec*span;end=start+round(2*BEAT*SR)
        alpha=np.linspace(0,1,end-start)[:,None];alpha=alpha*alpha*(3-2*alpha)
        demo[start:end]=(1-alpha)*mixes[src][start:end]+alpha*mixes[dst][start:end]
        demo[end:]=mixes[dst][end:n]
    demo[-2205:]*=np.linspace(1,0,2205)[:,None]
    w.wav(ROOT/'tidepool-demo.wav',demo)
    (ROOT/'render-info.json').write_text(json.dumps(report,indent=2)+'\n')
    with zipfile.ZipFile(ROOT/'tidepool-midi-source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in [ROOT/'generate.py',ROOT/'README.md',ROOT.parent/'contrasts/generate.py',*sorted(ROOT.glob('*.mid'))]:
            if p.exists():z.write(p,str(p.relative_to(ROOT.parent)))
    print(json.dumps(report,indent=2))
