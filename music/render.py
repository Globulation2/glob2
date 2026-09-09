"""Render MIDI trios with Apple's stock sampler and General MIDI sound bank.

Usage: python3 music/render.py [set-id ...] [--directory PATH] [--sound-bank PATH]
Requires macOS, Xcode command-line tools, and NumPy. No custom instrument synthesis.
"""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import tempfile
import numpy as np
from midi_tools import wav
from install_sets import SETS

ROOT = Path(__file__).resolve().parent
BANK = Path('/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls')
RATE = 44100


def read_midi(path):
    data = path.read_bytes()
    if data[:4] != b'MThd': raise ValueError(f'{path}: missing MIDI header')
    size = int.from_bytes(data[4:8], 'big')
    fmt, count, ppq = struct.unpack('>HHH', data[8:14])
    if fmt not in (0, 1) or not ppq or ppq & 0x8000: raise ValueError('Unsupported MIDI format/time division')
    pos = 8 + size; events = []; tempos = []; end_tick = 0
    for _ in range(count):
        if data[pos:pos+4] != b'MTrk': raise ValueError('Missing MIDI track')
        length = int.from_bytes(data[pos+4:pos+8], 'big')
        track = data[pos+8:pos+8+length]; pos += 8+length
        i = tick = 0; running = None
        def vlq():
            nonlocal i
            value = 0
            for _ in range(4):
                byte = track[i]; i += 1; value = (value << 7) | (byte & 127)
                if byte < 128: return value
            raise ValueError('Invalid MIDI variable-length value')
        while i < len(track):
            tick += vlq(); status = track[i]
            if status >= 128:
                i += 1
                if status < 240: running = status
            elif running is not None: status = running
            else: raise ValueError('MIDI running status without status byte')
            if status == 255:
                kind = track[i]; i += 1; length = vlq(); payload = track[i:i+length]; i += length
                if kind == 81: tempos.append((tick, int.from_bytes(payload, 'big')))
                if kind == 47: break
            elif status in (240, 247):
                length = vlq(); i += length; running = None
            elif status < 240:
                length = 1 if status & 240 in (192, 208) else 2
                payload = track[i:i+length]; i += length
                if len(payload) != length: raise ValueError('Truncated MIDI event')
                if status & 240 == 192 and tick != 0: raise ValueError('Program changes must occur at tick zero')
                events.append((tick, status, payload[0], payload[1] if length == 2 else 0))
            else: raise ValueError('Unsupported MIDI event')
        end_tick = max(end_tick, tick)
    # Convert every event and the end marker using the shared tempo map.
    timeline = [(t, 0, tempo) for t, tempo in sorted(set(tempos))]
    timeline += [(t, 1, (s, a, b)) for t, s, a, b in events]
    timeline.append((end_tick, 2, None))
    previous = 0; seconds = 0.; tempo = 500000; result = []; frames = 0
    for tick, kind, value in sorted(timeline, key=lambda x: (x[0], x[1])):
        seconds += (tick-previous)*tempo/(ppq*1e6); previous = tick
        frame = round(seconds*RATE)
        if kind == 0: tempo = value
        elif kind == 1:
            status, a, b = value
            result.append(dict(frame=frame, status=status, data1=a, data2=b))
        else: frames = frame
    if frames <= 0: raise ValueError('Empty MIDI loop')
    return frames, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sets', nargs='*', choices=None)
    parser.add_argument('--directory', type=Path, help='Render a standalone directory containing calm/building/combat.mid')
    parser.add_argument('--sound-bank', type=Path, default=BANK)
    args = parser.parse_args()
    if not args.sound_bank.is_file(): parser.error('Sound bank not found; pass --sound-bank with a DLS/SF2 file')
    selected = args.sets or ([] if args.directory else list(SETS))
    if set(selected)-set(SETS): parser.error('Unknown set: '+', '.join(sorted(set(selected)-set(SETS))))
    groups = [(key, ROOT/SETS[key][2], SETS[key][3]) for key in selected]
    if args.directory: groups.append((args.directory.name, args.directory.resolve(), ('calm','building','combat')))
    with tempfile.TemporaryDirectory(prefix='glob2-midi-') as temporary:
        temp = Path(temporary); binary = temp/'render-midi'
        subprocess.run(['xcrun','swiftc',str(ROOT/'RenderMIDI.swift'),'-o',str(binary)], check=True)
        jobs = []; descriptions = []
        for key, folder, names in groups:
            frames_list = []
            for name in names:
                frames, events = read_midi(folder/f'{name}.mid'); frames_list.append(frames)
                output = temp/f'{len(jobs)}.raw'
                jobs.append(dict(frames=frames, events=events, output=str(output)))
            if len(set(frames_list)) != 1: raise ValueError(f'{key}: mismatched MIDI loop lengths')
            descriptions.append((key, folder, names))
        request = temp/'request.json'
        request.write_text(json.dumps(dict(soundBank=str(args.sound_bank.resolve()), jobs=jobs)))
        subprocess.run([str(binary),str(request)], check=True)
        for group_index, (key, folder, names) in enumerate(descriptions):
            group_jobs = jobs[group_index*3:group_index*3+3]
            mixes = [np.fromfile(job['output'],dtype='<f4').reshape(-1,2) for job in group_jobs]
            for job, mix in zip(group_jobs,mixes):
                if len(mix) != job['frames'] or not np.isfinite(mix).all(): raise ValueError('Invalid renderer output')
            peak = max(float(np.max(np.abs(a))) for a in mixes)
            if peak <= 0: raise ValueError('Silent sound bank output')
            # One gain for the entire trio preserves arrangement dynamics.
            gain = .90/peak
            mixes = [a*gain for a in mixes]
            report = dict(renderer='Apple AVAudioUnitSampler', sound_bank=args.sound_bank.name,
                          sample_rate=RATE, shared_gain=gain, warmup_loops=2, tracks={})
            for name, mix in zip(names,mixes):
                wav(folder/f'{name}.wav',mix)
                report['tracks'][name] = dict(frames=len(mix), seconds=len(mix)/RATE,
                    peak=float(np.max(np.abs(mix))),rms=float(np.sqrt(np.mean(mix*mix))),
                    boundary_step=float(np.max(np.abs(mix[0]-mix[-1]))))
            span = len(mixes[0])//8; n = span*4; demo = mixes[0][:n].copy()
            for index,src,dst in ((1,0,1),(2,1,2),(3,2,0)):
                start = index*span; end = start+RATE
                alpha = np.linspace(0,1,RATE)[:,None]; alpha = alpha*alpha*(3-2*alpha)
                demo[start:end] = (1-alpha)*mixes[src][start:end]+alpha*mixes[dst][start:end]
                demo[end:] = mixes[dst][end:n]
            demo[-4410:] *= np.linspace(1,0,4410)[:,None]
            wav(folder/f'{key}-demo.wav',demo)
            report['preview_switch_seconds'] = [span/RATE*i for i in (1,2,3)]
            (folder/'render-info.json').write_text(json.dumps(report,indent=2)+'\n')
            print(key, 'rendered and verified', flush=True)


if __name__ == '__main__': main()
