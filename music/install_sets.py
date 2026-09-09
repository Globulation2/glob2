"""Encode the curated WAV trios with libsndfile and verify their decoded lengths.

Usage: python3 music/install_sets.py
Requires libsndfile with Ogg/Vorbis support (e.g. Homebrew libsndfile on macOS).
"""
from pathlib import Path
import ctypes as C
import ctypes.util
import json
import wave
import sys

ROOT=Path(__file__).resolve().parent
SETS={
 'seedling': ('Seedling', 'Organic electronic', 'seedling', ('a1-calm','a2-building','a3-combat')),
 'bramble-dance': ('Bramble Dance', 'Marimba, bamboo flute and hand drums', 'contrasts/bramble-dance', ('calm','building','combat')),
 'velvet-orbit': ('Velvet Orbit', 'Spacious electric piano and broken beats', 'contrasts/velvet-orbit', ('calm','building','combat')),
 'tidepool': ('Tidepool', 'Caribbean-inspired organic electronic', 'tidepool', ('calm','building','combat')),
}

class Info(C.Structure):
    _fields_=[('frames',C.c_int64),('samplerate',C.c_int),('channels',C.c_int),
              ('format',C.c_int),('sections',C.c_int),('seekable',C.c_int)]


def main():
    library=ctypes.util.find_library('sndfile')
    if not library and Path('/opt/homebrew/lib/libsndfile.dylib').exists():
        library='/opt/homebrew/lib/libsndfile.dylib'
    if not library: raise SystemExit('Install libsndfile with Ogg/Vorbis support first.')
    lib=C.CDLL(library)
    lib.sf_open.argtypes=[C.c_char_p,C.c_int,C.POINTER(Info)];lib.sf_open.restype=C.c_void_p
    for method in ('sf_writef_short','sf_readf_short'):
        f=getattr(lib,method);f.argtypes=[C.c_void_p,C.POINTER(C.c_short),C.c_int64];f.restype=C.c_int64
    lib.sf_close.argtypes=[C.c_void_p];lib.sf_close.restype=C.c_int
    lib.sf_strerror.argtypes=[C.c_void_p];lib.sf_strerror.restype=C.c_char_p
    selected=set(sys.argv[1:]) or set(SETS)
    if selected-set(SETS):raise SystemExit('Unknown music set: '+', '.join(sorted(selected-set(SETS))))
    manifest_path=ROOT/'installed-sets.json'
    manifest=json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest={key:value for key,value in manifest.items() if key in SETS}
    for key,(title,style,source,names) in SETS.items():
        if key not in selected:continue
        dest=ROOT.parent/'data/zik'/key;dest.mkdir(exist_ok=True)
        records=[]
        for index,name in enumerate(names,1):
            path=ROOT/source/f'{name}.wav'
            with wave.open(str(path)) as w:
                assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(2,2,44100)
                count=w.getnframes();raw=w.readframes(count)
            samples=(C.c_short*(len(raw)//2)).from_buffer_copy(raw)
            info=Info(0,44100,2,0x200000|0x60,0,0)  # Ogg container, Vorbis codec
            target=dest/f'a{index}.ogg'
            handle=lib.sf_open(str(target).encode(),0x20,C.byref(info))
            if not handle: raise RuntimeError(lib.sf_strerror(None).decode())
            try: assert lib.sf_writef_short(handle,samples,count)==count
            finally: assert lib.sf_close(handle)==0
            decoded=Info();handle=lib.sf_open(str(target).encode(),0x10,C.byref(decoded))
            assert handle,lib.sf_strerror(None)
            try:
                assert (decoded.frames,decoded.samplerate,decoded.channels)==(count,44100,2)
                assert decoded.format==0x200060
                block=(C.c_short*(8192*2))();total=0;peak=0
                while True:
                    n=lib.sf_readf_short(handle,block,8192)
                    assert n>=0
                    if not n:break
                    total+=n;peak=max(peak,max(abs(x) for x in block[:n*2]))
                assert total==count and 0<peak<32767
            finally: assert lib.sf_close(handle)==0
            records.append({'file':str(target.relative_to(ROOT.parent)),'source':str(path.relative_to(ROOT.parent)),
                            'frames':count,'seconds':count/44100,'decoded_peak':peak/32768})
        assert len({r['frames'] for r in records})==1
        manifest[key]={'title':title,'style':style,'tracks':records}
        print(key, 'encoded and decoded all three tracks; frame counts match',flush=True)
    (ROOT/'installed-sets.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__': main()
