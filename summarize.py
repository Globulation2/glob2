from collections import Counter
from pathlib import Path
import math
import re
import statistics

root = Path(__file__).resolve().parent
for path in sorted(root.glob('*/trace.log')):
    data = path.read_text()
    frames = {int(frame): float(elapsed) for frame, elapsed in re.findall(
        r'^FRAME .*?frame=(-?\d+) elapsed_ms=([\d.]+)', data, re.M)}
    samples = sorted(elapsed for frame, elapsed in frames.items() if frame >= 0)
    resolutions = Counter(int(res) for frame, res in re.findall(
        r'^FRAME_CACHE frame=(-?\d+) .*?resolution=(\d+)', data, re.M) if int(frame) >= 0)
    textures = max(map(int, re.findall(r'peak_texture_bytes=(\d+)', data)), default=0) / 2**20
    print(path.parent.name)
    print(' ', next((line for line in data.splitlines() if line.startswith('BACKEND ')), 'No renderer'))
    print(' ', next((line for line in data.splitlines() if line.startswith('simulation_checksum=')), 'No checksum'))
    if samples:
        print(f'  Cold {frames.get(-1)} ms, measured n={len(samples)}, median={statistics.median(samples):.2f} ms, '
              f'p95={samples[math.ceil(.95*len(samples))-1]:.2f} ms, max={max(samples):.2f} ms')
        print(f'  Page resolutions {dict(resolutions)}, peak terrain textures {textures:.2f} MiB')
