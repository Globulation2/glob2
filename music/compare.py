"""Create level-matched Apple/reference vs FluidSynth A/B clips and audio metrics.

Usage: python3 music/compare.py REFERENCE_DIRECTORY OUTPUT_DIRECTORY
Reference directory contains one subfolder per installed set, with its three WAVs.
"""
from pathlib import Path
import json
import sys
import wave
import numpy as np
from install_sets import SETS
from midi_tools import wav


def read(path):
    with wave.open(str(path)) as source:
        if (source.getframerate(),source.getnchannels(),source.getsampwidth()) != (44100,2,2):
            raise ValueError('Expected 44.1 kHz stereo PCM16')
        return np.frombuffer(source.readframes(source.getnframes()),dtype='<i2').astype(np.float32).reshape(-1,2)/32768


def rms(a): return float(np.sqrt(np.mean(a*a)))


def main():
    reference,output = map(Path,sys.argv[1:])
    output.mkdir(parents=True,exist_ok=True)
    report = {}
    root = Path(__file__).resolve().parent
    for key,(_,_,folder,names) in SETS.items():
        clips = []; measurements = {}
        for name in names:
            old = read(reference/key/f'{name}.wav'); new = read(root/folder/f'{name}.wav')
            if len(old) != len(new): raise ValueError(f'{key}: timing changed')
            # Compare musical energy at 100ms resolution; waveforms differ by instrument bank.
            n = len(old)//4410*4410
            envelopes = [np.sqrt(np.mean(a[:n].reshape(-1,4410,2)**2,axis=(1,2))) for a in (old,new)]
            measurements[name] = dict(frames=len(new),reference_rms=rms(old),fluidsynth_rms=rms(new),
                energy_envelope_correlation=float(np.corrcoef(*envelopes)[0,1]),
                peak=float(np.max(np.abs(new))))
            pair = [a[:8*44100].copy() for a in (old,new)]
            for a in pair: a *= .12/rms(a)
            safety = min(1.,.90/max(float(np.max(np.abs(a))) for a in pair))
            for a in pair:
                a *= safety
                a[:441] *= np.linspace(0,1,441)[:,None]
                a[-441:] *= np.linspace(1,0,441)[:,None]
                clips.append(a)
        wav(output/f'{key}-ab.wav',np.concatenate(clips))
        report[key] = measurements
        print(key, 'A/B ready; envelope correlations:',[round(x['energy_envelope_correlation'],2) for x in measurements.values()])
    (output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__': main()
