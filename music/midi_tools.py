"""Standard MIDI export and PCM WAV writing; no instrument synthesis."""
import struct
import wave
SR, PPQ = 44100, 480

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
    import numpy as np

    if not np.isfinite(a).all() or np.max(np.abs(a)) >= 1:
        raise ValueError('PCM samples must be finite and below full scale')
    with wave.open(str(path),'wb') as f:
        f.setparams((2,2,SR,0,'NONE','not compressed'))
        f.writeframes((a*32767).astype('<i2').tobytes())
