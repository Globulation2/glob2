#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare optional SHADER_PROOF_PIXELS WebGL/WebGPU fixtures, normalizing rows.

One channel value (1/255) is the declared SwiftShader shader-conformance tolerance.
This shader test does not define scene/hardware rollout tolerances.
"""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
metadata = json.loads((args.directory / 'metadata.json').read_text())
results = []
for material in metadata['materials']['materials']:
    identity = material['id']
    reference = (args.directory / f'webgl-skin-{identity}.rgba').read_bytes()
    output = (args.directory / f'webgpu-skin-{identity}.rgba').read_bytes()
    if len(reference) != 128 * 128 * 4 or len(output) != len(reference):
        raise ValueError('wrong fixture dimensions')
    normalized = b''.join(output[y * 512:(y + 1) * 512] for y in range(127, -1, -1))
    differences = [abs(a - b) for a, b in zip(reference, normalized)]
    results.append({'material': material['key'], 'maximum': max(differences),
                    'changedChannels': sum(value > 0 for value in differences)})
receipt = {'sourceRevision': metadata['sourceRevision'], 'tolerance': 1, 'results': results}
(args.directory / 'webgpu-webgl-parity.json').write_text(json.dumps(receipt, indent=2) + '\n')
if any(result['maximum'] > receipt['tolerance'] for result in results):
    raise SystemExit('Skin shader parity exceeded declared tolerance')
print(f'{len(results)} skin materials passed cross-API pixel parity (maximum channel tolerance 1/255)')
