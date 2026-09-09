"""Offline FluidSynth adapter. Instrument synthesis is entirely in libfluidsynth."""
import ctypes as C
import ctypes.util
import os
import shutil
from pathlib import Path
import numpy as np

SETTINGS = {'synth.sample-rate':44100., 'synth.gain':.2, 'synth.polyphony':256,
            'synth.cpu-cores':1, 'synth.reverb.active':1, 'synth.chorus.active':0,
            'synth.reverb.room-size':.35, 'synth.reverb.damp':.4,
            'synth.reverb.width':.5, 'synth.reverb.level':.15,
            'synth.min-note-length':0, 'synth.midi-bank-select':'gm'}


class FluidRenderer:
    def __init__(self):
        path = os.environ.get('FLUIDSYNTH_LIBRARY') or ctypes.util.find_library('fluidsynth')
        if not path:
            executable = shutil.which('fluidsynth')
            if executable:
                prefix = Path(executable).resolve().parent.parent
                for candidate in (prefix/'lib/libfluidsynth.dylib', prefix/'bin/libfluidsynth-3.dll'):
                    if candidate.exists(): path = str(candidate); break
        if not path: raise RuntimeError('Install FluidSynth, or set FLUIDSYNTH_LIBRARY to its shared library')
        self.lib = lib = C.CDLL(path)
        def bind(name, result, *args):
            function = getattr(lib,name); function.restype = result; function.argtypes = list(args)
        bind('fluid_version_str',C.c_char_p)
        bind('new_fluid_settings',C.c_void_p)
        bind('delete_fluid_settings',None,C.c_void_p)
        for suffix,kind in [('int',C.c_int),('num',C.c_double),('str',C.c_char_p)]:
            bind('fluid_settings_set'+suffix,C.c_int,C.c_void_p,C.c_char_p,kind)
        bind('new_fluid_synth',C.c_void_p,C.c_void_p)
        bind('delete_fluid_synth',None,C.c_void_p)
        bind('fluid_synth_sfload',C.c_int,C.c_void_p,C.c_char_p,C.c_int)
        bind('fluid_synth_set_interp_method',C.c_int,C.c_void_p,C.c_int,C.c_int)
        for name,count in [('noteon',3),('noteoff',2),('cc',3),('program_change',2),
                           ('pitch_bend',2),('channel_pressure',2),('key_pressure',3)]:
            bind('fluid_synth_'+name,C.c_int,C.c_void_p,*([C.c_int]*count))
        bind('fluid_synth_write_float',C.c_int,C.c_void_p,C.c_int,C.c_void_p,C.c_int,C.c_int,C.c_void_p,C.c_int,C.c_int)
        self.version = lib.fluid_version_str().decode()

    @staticmethod
    def check(result):
        if result < 0: raise RuntimeError('FluidSynth rejected a render operation')

    def render(self, frames, events, sound_bank):
        lib = self.lib; settings = lib.new_fluid_settings(); synth = None
        if not settings: raise RuntimeError('Could not create FluidSynth settings')
        try:
            for name,value in SETTINGS.items():
                suffix = 'str' if isinstance(value,str) else 'int' if isinstance(value,int) else 'num'
                self.check(getattr(lib,'fluid_settings_set'+suffix)(settings,name.encode(),value.encode() if suffix=='str' else value))
            synth = lib.new_fluid_synth(settings)
            if not synth: raise RuntimeError('Could not create FluidSynth')
            self.check(lib.fluid_synth_sfload(synth,os.fsencode(sound_bank),1))
            self.check(lib.fluid_synth_set_interp_method(synth,-1,4))
            result = np.empty((frames,2),dtype=np.float32)
            scratch = np.empty((8192,2),dtype=np.float32)
            def event(e, repeat):
                channel = e['status'] & 15; kind = e['status'] & 240; a=e['data1']; b=e['data2']
                if kind==192 and repeat: return
                name, args = {128:('noteoff',(channel,a)),144:('noteon',(channel,a,b)),
                    160:('key_pressure',(channel,a,b)),176:('cc',(channel,a,b)),
                    192:('program_change',(channel,a)),208:('channel_pressure',(channel,a)),
                    224:('pitch_bend',(channel,a+b*128))}[kind]
                status = getattr(lib,'fluid_synth_'+name)(synth,*args)
                # One-shot samples can finish naturally before their MIDI note-off.
                if status < 0 and name != 'noteoff' and not (name == 'noteon' and b == 0):
                    raise RuntimeError(f'FluidSynth rejected {name}{args}')
            for repeat in range(3):
                position = 0
                for e in events+[dict(frame=frames,status=0)]:
                    target = min(e['frame'],frames)
                    while position < target:
                        n = min(8192,target-position)
                        block = result[position:position+n] if repeat==2 else scratch[:n]
                        pointer = block.ctypes.data
                        self.check(lib.fluid_synth_write_float(synth,n,pointer,0,2,pointer,1,2))
                        position += n
                    if e['status']: event(e,repeat)
            return result
        finally:
            if synth: lib.delete_fluid_synth(synth)
            lib.delete_fluid_settings(settings)
