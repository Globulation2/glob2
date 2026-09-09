"""Independent palettes, meters, tempi and compositions; NumPy is the only dependency.

Run python3 music/contrasts/generate.py. MIDI is the editable note source;
the custom synth below defines the audition timbres, not a GM soundfont.
"""
from pathlib import Path
import json
import struct
import wave
import zipfile
import numpy as np

ROOT = Path(__file__).resolve().parent
SR, PPQ = 44100, 480
STATES = ('calm', 'building', 'combat')
SETS = {
 'bramble-dance': dict(bpm=108, bars=40, beats=3, meter=(6, 3),
   parts=[('lute',24,0,-.3),('flute',73,1,.25),('fiddle',48,2,.1),('upright',43,3,0),('hand',0,9,0)],
   description='Acoustic folk dance in 6/8: plucked strings, breathy flute, bowed strings and hand drums.'),
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
            # Six eighth-notes, grouped 3+3. No synth pad or electronic bass.
            pattern = [0,1,2,1,2,1] if bar%2==0 else [2,1,0,1,2,0]
            for j,k in enumerate(pattern):
                add('lute',b+j*.5,chord[k],.43,65 if j%3==0 else 49)
            for off in (0,1.5): add('upright',b+off,root,1.2,65+level*9)
            melody = ([74,76,78,81,78,76] if bar%2==0 else [78,76,74,73,74,69])
            # Four-bar melodic answers are transposed down rather than repeated.
            shift = -5 if (bar//4)%2 else 0
            if bar%8 != 7:
                for j,p in enumerate(melody):
                    if level<2 and j in (0,2,3,5): add('flute',b+j*.5,p+shift,.42,51)
                    if level==2: add('fiddle',b+j*.5,p+shift-12,.36,84)
            if level>=1:
                for off,p in ((0,41),(1,60),(1.5,41),(2.5,62)):
                    add('hand',b+off,p,.22,65+level*12)
                for j in range(6): add('hand',b+j*.5,70,.08,29+level*8)
            if level==2:
                for off in (0,.75,1.5,2.25): add('hand',b+off,36,.25,105)
                # Low plucked accents support the urgent bowed-string reel.
                for off in (0,1.5):
                    add('lute',b+off,root+12,.24,85)
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
                # Laid-back swung hats, unlike the straight folk and machine sets.
                for off in (0,.66,1,1.66,2,2.66,3,3.66): add('breaks',b+off,42,.07,32+level*5)
            if level==2:
                for off in (.75,1.75,2.25,3.5): add('breaks',b+off,36,.22,101)
                for off in (1.75,3.75): add('breaks',b+off,38,.12,53)
                for off in (1.5,3.5): add('sub',b+off,root-12,.3,102)
    return notes


def tone(part,p,d,sr,rng,level):
    tail = .9 if part in ('piano','glass','air') else .16
    t=np.arange(round((d+tail)*sr))/sr
    f=440*2**((p-69)/12); ph=2*np.pi*f*t
    gate=np.exp(-np.maximum(t-d,0)*30)
    attack=1-np.exp(-t*350)
    if part=='lute':
        y=sum(np.sin(ph*k + .06*k)*np.exp(-t*(2+k*.8))/k for k in range(1,14))
        env=attack*gate; gain=.25
    elif part=='flute':
        y=np.sin(ph+.025*np.sin(2*np.pi*5*t))+.12*np.sin(ph*2)+.012*rng.normal(size=len(t))
        env=(1-np.exp(-t*35))*gate; gain=.15
    elif part=='fiddle':
        y=sum(np.sin(ph*k+.035*k*np.sin(2*np.pi*5.5*t))/k for k in range(1,15))
        env=(1-np.exp(-t*65))*gate; gain=.13
    elif part=='upright':
        y=np.sin(ph)+.35*np.sin(ph*2)*np.exp(-t*9)+.2*np.sin(ph*3)*np.exp(-t*14)
        env=attack*np.exp(-t*3)*gate; gain=.35
    elif part=='piano':
        y=np.sin(ph+1.5*np.sin(ph*3)*np.exp(-t*7))+.22*np.sin(ph*2)*np.exp(-t*3)
        env=attack*np.exp(-t*1.8)*gate; gain=.20
    elif part=='glass':
        y=np.sin(ph)+.32*np.sin(ph*2.01)*np.exp(-t*3)+.13*np.sin(ph*3.98)*np.exp(-t*5)
        env=attack*np.exp(-t*2.5)*gate; gain=.18
    elif part=='air':
        y=np.sin(ph)+.3*np.sin(ph*1.002)+.08*np.sin(ph*2)
        env=np.minimum(t/.7,1)*np.exp(-np.maximum(t-d,0)*4); gain=.11
    elif part=='sub':
        y=np.sin(ph) if level<2 else np.tanh(2.2*(np.sin(ph)+.15*np.sin(ph*2)))
        env=(1-np.exp(-t*70))*gate; gain=.33
    elif part in ('acid','drive'):
        cutoff=2+10*np.exp(-t*(15 if part=='acid' else 3))
        y=sum(np.sin(ph*k)*np.exp(-k/cutoff)/k**.65 for k in range(1,22))
        y=np.tanh(y*(1.2 if part=='acid' else 4))
        env=attack*gate*np.exp(-t*(6 if part=='acid' else 1.8)); gain=.22 if part=='acid' else .32
    elif part=='metal':
        y=sum(np.sin(ph*k)*np.exp(-t*k*4)/k for k in (1,1.414,2.71,4.13))
        env=attack*gate; gain=.17
    elif part=='siren':
        y=sum(np.sin(ph*k+.4*k*np.sin(2*np.pi*2*t))/k for k in range(1,9))
        env=(1-np.exp(-t*12))*gate; gain=.12
    else:
        noise=rng.uniform(-1,1,len(t))
        if p==36:
            y=np.sin(2*np.pi*(45*t+2*(1-np.exp(-t*45))))
            if part=='machine': y=np.tanh(y*3)+.16*noise*np.exp(-t*140)
            env=(1-np.exp(-t*1400))*np.exp(-t*(13 if part!='hand' else 18))
        elif p==38:
            y=noise*.7+.35*np.sin(2*np.pi*185*t)
            env=(1-np.exp(-t*1000))*np.exp(-t*(24 if part=='machine' else 18))
        elif p in (42,49,70):
            y=np.r_[0,np.diff(noise)]*.5
            env=(1-np.exp(-t*1500))*np.exp(-t*(8 if p==49 else 85))
        else:
            drumfreq={41:85,60:230,62:310}.get(p,140)
            y=np.sin(2*np.pi*drumfreq*t)+.27*np.sin(2*np.pi*drumfreq*1.59*t)+.07*noise
            env=(1-np.exp(-t*900))*np.exp(-t*19)
        gain=.36
    return y*env*gain


def render(key,c,notes,level):
    beat=60/c['bpm']; n=round(c['bars']*c['beats']*beat*SR)
    audio=np.zeros((n,2),dtype=np.float32); rng=np.random.default_rng(808)
    for part,_,_,pan in c['parts']:
        for b,p,d,v in notes[part]:
            y=tone(part,p,d*beat,SR,rng,level)*v/127
            idx=(round(b*beat*SR)+np.arange(len(y)))%n
            audio[idx,0]+=y*np.sqrt((1-pan)/2)
            audio[idx,1]+=y*np.sqrt((1+pan)/2)
    # Separate acoustic spaces, not one shared delay on every composition.
    dry=audio.copy()
    echoes={'bramble-dance':[(.043,.08),(.079,.05)],
            'velvet-orbit':[(beat*.75,.23),(beat*1.5,.13),(beat*2.25,.07)]}[key]
    for delay,gain in echoes: audio+=gain*np.roll(dry[:,::-1],round(delay*SR),axis=0)
    target=(.13,.17,.24)[level]
    audio*=target/float(np.sqrt(np.mean(audio**2)))
    audio=.94*np.tanh(audio/.94)
    return audio


def vlq(n):
    a=[n&127]
    while n>>7: n>>=7; a.insert(0,(n&127)|128)
    return bytes(a)


def midi(c,notes,path):
    end=c['bars']*c['beats']*PPQ
    events=[[(0,b'\xff\x51\x03'+round(60e6/c['bpm']).to_bytes(3,'big')),
             (0,b'\xff\x58\x04'+bytes([*c['meter'],36 if c['beats']==3 else 24,8]))]]
    for name,program,ch,pan in c['parts']:
        ev=[(0,b'\xff\x03'+vlq(len(name))+name.encode()),(0,bytes([0xc0|ch,program])),
            (0,bytes([0xb0|ch,10,round(64+pan*63)]))]
        active={}
        for b,p,d,v in sorted(notes[name]):
            assert 0<=p<=127 and 0<v<=127 and d>0 and b+d<=c['bars']*c['beats']
            assert b>=active.get(p,0),(name,b,p,'overlap')
            active[p]=b+d
            ev.extend([(round(b*PPQ),bytes([0x90|ch,p,v])),(round((b+d)*PPQ),bytes([0x80|ch,p,0]))])
        events.append(ev)
    tracks=[]
    for ev in events:
        prev=0; data=bytearray()
        for tick,payload in sorted(ev,key=lambda e:(e[0],e[1][0]>>4==9)):
            data+=vlq(tick-prev)+payload;prev=tick
        data+=vlq(end-prev)+b'\xff\x2f\x00'
        tracks.append(b'MTrk'+struct.pack('>I',len(data))+data)
    path.write_bytes(b'MThd'+struct.pack('>IHHH',6,1,len(tracks),PPQ)+b''.join(tracks))


def wav(path,a):
    assert np.isfinite(a).all() and np.max(np.abs(a))<1
    with wave.open(str(path),'wb') as f:
        f.setparams((2,2,SR,0,'NONE','not compressed'))
        f.writeframes((a*32767).astype('<i2').tobytes())


if __name__=='__main__':
    for key,c in SETS.items():
        folder=ROOT/key;folder.mkdir(exist_ok=True)
        mixes=[]; report={'bpm':c['bpm'],'meter':'6/8' if c['beats']==3 else '4/4','tracks':{}}
        for level,state in enumerate(STATES):
            notes=compose(key,c,level)
            midi(c,notes,folder/f'{state}.mid')
            a=render(key,c,notes,level);wav(folder/f'{state}.wav',a);mixes.append(a)
            report['tracks'][state]={'seconds':len(a)/SR,'peak':float(np.max(np.abs(a))),
                                    'rms':float(np.sqrt(np.mean(a*a))),'boundary_step':float(np.max(np.abs(a[0]-a[-1])))}
        n=36*SR; demo=mixes[0][:n].copy()
        for sec,src,dst in ((9,0,1),(18,1,2),(27,2,0)):
            start=sec*SR;end=start+SR
            alpha=np.linspace(0,1,SR)[:,None];alpha=alpha*alpha*(3-2*alpha)
            demo[start:end]=(1-alpha)*mixes[src][start:end]+alpha*mixes[dst][start:end]
            demo[end:]=mixes[dst][end:n]
        demo[-2205:]*=np.linspace(1,0,2205)[:,None]
        wav(folder/f'{key}-demo.wav',demo)
        (folder/'render-info.json').write_text(json.dumps(report,indent=2)+'\n')
        print(key,json.dumps(report),flush=True)
    with zipfile.ZipFile(ROOT/'contrasts-midi-source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in [ROOT/'generate.py',ROOT/'README.md',*sorted(ROOT.glob('*/*.mid'))]:
            if p.exists():z.write(p,str(p.relative_to(ROOT.parent)))
