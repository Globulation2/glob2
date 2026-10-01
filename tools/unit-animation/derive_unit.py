#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive new unit artwork from the original glob Blender 2.34 rigs.

A new unit (for example the wizard concept in ``wizard_unit.py``) is described
as a set of edits to copies of the original warrior ``.blend`` files:

* extra metaball elements on the body, and changes to the body element itself
  (this is how the wizard's skin becomes a pointed oval);
* new sizes and positions for the hand/foot and elbow/knee metaball objects
  (thin limbs);
* new keyframes for the limb bones and for the armature object (a new attack
  or swim stroke).

Everything is applied *inside Blender 2.34* by a generated Python 2.3 script,
and the result is saved by Blender 2.34 itself. Loading and re-saving an
original source through this path renders pixel-identically to the shipped
sprites, so derived units keep the exact house style: same skin material,
lights, camera, shadow pass and renderer.

Blender 2.34's embedded Python has no standard library (not even ``math``),
so all geometry and quaternion maths runs here, in host Python 3; the
generated script only receives numbers.

Typical use, from the repository root (see README.md, "Deriving new units"):

    python3 tools/unit-animation/derive_unit.py build  --work /path/to/work
    python3 tools/unit-animation/derive_unit.py render --work /path/to/work --size 40
    python3 tools/unit-animation/derive_unit.py render --work /path/to/work --size 128

The work directory must be mounted in the Blender 2.34 render container (see
``Dockerfile.blender234``) at ``--container-work``.
"""
import argparse
import concurrent.futures
import importlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ORIGINALS = ROOT / 'datasrc/gfx/originals/units'
sys.path.insert(0, str(HERE))
import render  # noqa: E402  (sibling module: Blender 2.34 DNA parsing)

# --------------------------------------------------------------------------
# Facts about the original warrior rigs that the edits rely on.
# --------------------------------------------------------------------------

# Scene frame f of these sources corresponds to IPO/action time f * 8 / 9
# (the original keys sit at 0.889 for frame 1 and 8.0 for frame 9). One
# animation cycle per direction spans scene frames 1..9.
def ipo_time(frame):
    return frame * 8.0 / 9.0


# Blender 2.34 stores pose quaternions in action channels whose names are
# shifted by one: the "QuatX" curve holds w, "QuatY" holds x, "QuatZ" holds y
# and "QuatW" holds z. Values below are always (w, x, y, z).
QUAT_CHANNELS = ['QuatX', 'QuatY', 'QuatZ', 'QuatW']

# The four limbs: upper bone -> (rest tail direction in armature space, forearm
# bone). Armature space while standing: +x to the glob's side, +y up, -z
# forward. Every bone head sits at the body centre.
LIMBS = {
    'Bone.004': ((2.96, 3.02, 0.0), 'Bone.005'),    # right arm
    'Bone.006': ((-3.02, 3.02, 0.0), 'Bone.007'),   # left arm
    'Bone.002': ((2.96, -3.02, 0.0), 'Bone.003'),   # right leg
    'Bone': ((-3.02, -2.96, 0.0), 'Bone.001'),      # left leg
}

# Hand/foot metaball objects (bone-parented to the forearms) and elbow/knee
# objects (parented to the upper bones), with their original locations.
HANDS = {'Mball.004': (0, 5, 5), 'Mball.003': (0, -5, 5), 'Mball.001': (0, 5, -5), 'Mball.002': (0, -5, -5)}
ELBOWS = {'Mball.005': (0, 3, 3), 'Mball.006': (0, -3, 3), 'Mball.007': (0, -3, -3), 'Mball.008': (0, 3, -3)}

# The body is metaball data Meta.014 on object "Mball" (scale 3.12). Its own
# element is a ball of radius 2 at the origin. In body-local coordinates +y is
# up, +x is backward (-x forward) and z is sideways.
BODY_DATA = 'Meta.014'
# The armature object's IPO (bob, lunge, and in the walk the 180-degree flip).
ARMATURE_IPO = 'ObIpo.016'

# Blender cannot delete keys through the 2.34 API, so a curve that already has
# N keys must be given at least N new ones (see pad()). These are the counts
# in the warrior sources; any other channel has at most two keys.
MIN_KEYS = {'QuatX': 4, 'QuatY': 4, 'QuatZ': 4, 'QuatW': 4, 'LocX': 3, 'LocY': 4, 'LocZ': 4}


# --------------------------------------------------------------------------
# Small vector / quaternion helpers (host side).
# --------------------------------------------------------------------------

def norm(v):
    length = math.sqrt(sum(x * x for x in v))
    return tuple(x / length for x in v)


def arc(u, v):
    """Shortest-arc quaternion (w, x, y, z) rotating unit vector u onto v."""
    cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    dot = sum(a * b for a, b in zip(u, v))
    return norm((1.0 + dot,) + cross)


def rotz(v, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (c * v[0] - s * v[1], s * v[0] + c * v[1], v[2])


def mirror_x(direction):
    """Left-hand limb direction from a right-hand one."""
    return [-direction[0], direction[1], direction[2]]


def pad(keys, n):
    """Insert interpolated keys (into the widest gaps) until there are >= n.

    Existing IPO curves cannot lose points through the 2.34 API, so a new
    animation must supply at least as many keys as the curve already has."""
    keys = list(keys)
    while len(keys) < n:
        i = max(range(len(keys) - 1), key=lambda k: keys[k + 1][0] - keys[k][0])
        a, b = keys[i], keys[i + 1]
        keys.insert(i + 1, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    return keys


# --------------------------------------------------------------------------
# Turning a unit spec into concrete edits.
# --------------------------------------------------------------------------

def limb_quaternion(bone, direction):
    """Pose quaternion that points `bone` along `direction` (armature space).

    Upper bones are roots whose rest frame is a rotation about z taking +y to
    the rest tail direction, so the pose rotation is the shortest arc from +y
    to the target expressed in that rest frame."""
    rest = LIMBS[bone][0]
    theta = math.atan2(-rest[0], rest[1])
    return arc((0.0, 1.0, 0.0), rotz(norm(direction), -theta))


def pose_curves(pose):
    """IPO curve edits for one animation set.

    `pose['limbs']` maps an upper bone to [(scene frame, direction), ...];
    the matching forearm is held straight. `pose['body']` maps an armature
    object channel (RotX in 10-degree units, LocY, LocZ, ...) to
    [(scene frame, value), ...]. Returns (ipo, channel, keys, extrapolation)."""
    curves = []
    for bone, keys in pose.get('limbs', {}).items():
        per_channel = {c: [] for c in QUAT_CHANNELS}
        for frame, direction in keys:
            for channel, value in zip(QUAT_CHANNELS, limb_quaternion(bone, direction)):
                per_channel[channel].append((ipo_time(frame), value))
        curves += [('Action.001.' + bone, c, per_channel[c], 'Cyclic_extrapolation') for c in QUAT_CHANNELS]
        forearm = LIMBS[bone][1]
        for channel, value in zip(QUAT_CHANNELS, (1.0, 0.0, 0.0, 0.0)):
            curves.append(('Action.001.' + forearm, channel, [(ipo_time(1), value), (ipo_time(9), value)],
                           'Cyclic_extrapolation'))
    for channel, keys in pose.get('body', {}).items():
        curves.append((ARMATURE_IPO, channel, [(ipo_time(f), v) for f, v in keys], 'Cyclic'))
    return [(ipo, c, pad(k, MIN_KEYS.get(c, 2)), e) for ipo, c, k, e in curves]


def limb_edits(limbs):
    """Object size/location edits for thin limbs.

    `hand` scales the hand/foot balls, `limb` is the (x, y, z) size of the
    elbow/knee balls (stretched along local x, which runs along the limb, so
    they bridge body and hand), and `reach` / `elbow_reach` move them further
    out along their original direction."""
    sizes = [[name, [limbs['hand']] * 3] for name in HANDS]
    sizes += [[name, list(limbs['limb'])] for name in ELBOWS]
    locations = [[name, [c * limbs['reach'] for c in loc]] for name, loc in HANDS.items()]
    locations += [[name, [c * limbs['elbow_reach'] for c in loc]] for name, loc in ELBOWS.items()]
    return sizes, locations


def script_parameters(unit, animation, out):
    """Everything the Blender 2.34 script needs, as plain JSON-able data."""
    sizes, locations = limb_edits(unit['limbs'])
    return dict(body_data=BODY_DATA, body_elements=unit['body']['elements'],
                body_element=unit['body'].get('own_element', []),
                sizes=sizes, locations=locations,
                curves=pose_curves(animation) if animation else [], out=out)


# --------------------------------------------------------------------------
# The generated Blender 2.34 script (Python 2.3: no conditional expressions,
# no f-strings, no `with`, no stdlib modules).
# --------------------------------------------------------------------------

BLENDER_SCRIPT = r'''
import Blender
from Blender import Object, Ipo, Metaball
P = %(params)s

def report(*words):
    print "DERIVE", " ".join([str(w) for w in words])

# 1. Body shape. Metaball.addMetaelem() in 2.34 prepends the new element and
#    garbles several fields, so add placeholders first, then set every field
#    explicitly. The original body element stays last.
body = Metaball.Get(P['body_data'])
for e in P['body_elements']:
    body.addMetaelem([0, 0.0, 0.0, 0.0, 1.0, 2.0, 0, 1.0, 1.0, 1.0])
for i in range(len(P['body_elements'])):
    x, y, z, rad, stiffness, kind, ex, ey, ez = P['body_elements'][i]
    for k, v in [('type', kind), ('x', x), ('y', y), ('z', z), ('rad', rad), ('s', stiffness),
                 ('expx', ex), ('expy', ey), ('expz', ez)]:
        body.setMetadata(k, i, v)
last = body.getNMetaElems() - 1
for k, v in P['body_element']:
    body.setMetadata(k, last, v)
report('body elements', body.getNMetaElems())

# 2. Limbs: move, then resize, the hand/foot and elbow/knee metaball objects.
for name, loc in P['locations']:
    Object.Get(name).setLocation(loc[0], loc[1], loc[2])
for name, size in P['sizes']:
    Object.Get(name).setSize(size[0], size[1], size[2])

# 3. Animation. Ipo.addCurve() is broken in 2.34, so only existing curves are
#    edited: existing points are moved, extra keys are appended (keys arrive
#    sorted and at least as many as the curve already has).
def set_curve(ipo, name, keys, extrapolation):
    curve = None
    for c in ipo.getCurves():
        if c.getName() == name:
            curve = c
    if curve is None:
        report('MISSING CURVE', ipo.getName(), name)
        return
    points = curve.getPoints()
    if len(points) > len(keys):
        report('TOO FEW KEYS', ipo.getName(), name)
    for i in range(len(keys)):
        if i < len(points):
            points[i].setPoints((keys[i][0], keys[i][1]))
        else:
            curve.addBezier((keys[i][0], keys[i][1]))
    curve.setInterpolation('Bezier')
    curve.setExtrapolation(extrapolation)
    curve.Recalc()

for ipo_name, channel, keys, extrapolation in P['curves']:
    set_curve(Ipo.Get(ipo_name), channel, keys, extrapolation)

Blender.Save(P['out'], 1)
report('saved', P['out'])
'''


def blender_literal(value):
    """JSON rendered as a Python 2.3 literal (true/false/null spelled for Python)."""
    return json.dumps(value).replace('true', '1').replace('false', '0').replace('null', 'None')


# --------------------------------------------------------------------------
# Running Blender 2.34 in the render container.
# --------------------------------------------------------------------------

class Renderer:
    """Runs the i386 Blender 2.34 binary under qemu in a Docker container that
    mounts `work` at `container_work` (see Dockerfile.blender234)."""

    def __init__(self, work, container, container_work, blender):
        self.work, self.container = Path(work).resolve(), container
        self.container_work, self.blender = container_work.rstrip('/'), blender

    def cpath(self, path):
        return self.container_work + '/' + str(Path(path).resolve().relative_to(self.work))

    def run(self, blend, *args):
        command = ['docker', 'exec', '-e', 'LD_LIBRARY_PATH=/opt/legacy/usr/lib', self.container,
                   'qemu-i386', self.blender, '-b', self.cpath(blend)] + list(args)
        result = subprocess.run(command, capture_output=True, text=True)
        return result.stdout + result.stderr

    def frames(self, blend, frames, workers):
        """Render the given frame numbers, in contiguous chunks across workers."""
        chunks = []
        for frame in sorted(frames):
            if chunks and frame == chunks[-1][-1] + 1 and len(chunks[-1]) < 16:
                chunks[-1].append(frame)
            else:
                chunks.append([frame])
        with concurrent.futures.ThreadPoolExecutor(workers) as pool:
            list(pool.map(lambda c: self.run(blend, '-s', str(c[0]), '-e', str(c[-1]), '-a'), chunks))


def prepare_scene(source, scene, output_dir, renderer, pixels, shadow, samples=4):
    """Copy a .blend with the same RenderData patch as render.py's prepare():
    `samples` poses per original frame, square `pixels` output, output path."""
    data = bytearray(Path(source).read_bytes())
    structures, blocks, offset = render.schema_and_scene(data)
    fields = structures['RenderData']

    def get(field, fmt):
        return struct.unpack_from('<' + fmt, data, offset + fields[field][0])[0]

    def put(field, fmt, value):
        struct.pack_into('<' + fmt, data, offset + fields[field][0], value)

    put('xsch', 'h', pixels)
    put('ysch', 'h', pixels)
    put('framapto', 'h', get('framapto', 'h') // samples)
    put('framelen', 'f', get('framelen', 'f') / samples)
    put('sfra', 'h', samples)
    put('efra', 'h', (128 if shadow else 64) * samples + samples - 1)
    path = (renderer.cpath(output_dir) + '/').encode() + b'\0'
    start, length = fields['pic']
    if len(path) > length:
        raise ValueError('Render output path exceeds the Blender field length')
    data[offset + start:offset + start + length] = path.ljust(length, b'\0')
    Path(scene).parent.mkdir(parents=True, exist_ok=True)
    Path(scene).write_bytes(data)


def team_frames(shadow, samples=4):
    """Rendered frame numbers: team layer 4..259, then shadow layer 260..515."""
    return list(range(samples, (128 if shadow else 64) * samples + samples))


# --------------------------------------------------------------------------
# Commands.
# --------------------------------------------------------------------------

def load_unit(name):
    return importlib.import_module(name + '_unit').UNIT


def build(args, renderer):
    """Write the unit's .blend sources to WORK/sources/."""
    unit = load_unit(args.unit)
    (renderer.work / 'originals').mkdir(parents=True, exist_ok=True)
    (renderer.work / 'scripts').mkdir(exist_ok=True)
    (renderer.work / 'sources').mkdir(exist_ok=True)
    for entry in unit['sets']:
        source = renderer.work / 'originals' / entry['source']
        shutil.copyfile(ORIGINALS / entry['source'], source)
        out = renderer.work / 'sources' / (entry['name'] + '.blend')
        params = script_parameters(unit, entry.get('animation'), renderer.cpath(out))
        script = renderer.work / 'scripts' / (entry['name'] + '.py')
        script.write_text(BLENDER_SCRIPT % dict(params=blender_literal(params)))
        output = renderer.run(source, '-P', renderer.cpath(script))
        lines = [l for l in output.splitlines() if l.startswith('DERIVE')]
        if not any(l.startswith('DERIVE saved') for l in lines) or any('MISSING' in l or 'TOO FEW' in l for l in lines):
            raise RuntimeError(entry['name'] + ':\n' + output[-3000:])
        print(entry['name'], 'built:', out)


def render_sets(args, renderer):
    """Render WORK/render<size>/<set>/NNNN.png, skipping frames already present."""
    unit = load_unit(args.unit)
    for entry in unit['sets']:
        if args.sets and entry['name'] not in args.sets:
            continue
        source = renderer.work / 'sources' / (entry['name'] + '.blend')
        scene = renderer.work / ('scenes%d' % args.size) / (entry['name'] + '.blend')
        output = renderer.work / ('render%d' % args.size) / entry['name']
        output.mkdir(parents=True, exist_ok=True)
        prepare_scene(source, scene, output, renderer, args.size, entry['shadow'])
        missing = [n for n in team_frames(entry['shadow']) if not (output / ('%04d.png' % n)).exists()]
        print(entry['name'], args.size, 'px:', len(missing), 'frames to render', flush=True)
        renderer.frames(scene, missing, args.workers)
        left = [n for n in team_frames(entry['shadow']) if not (output / ('%04d.png' % n)).exists()]
        if left:
            raise RuntimeError('%s: %d frames still missing' % (entry['name'], len(left)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=['build', 'render'])
    parser.add_argument('--unit', default='wizard', help='unit spec module <unit>_unit.py beside this script')
    parser.add_argument('--work', type=Path, required=True, help='host work directory, mounted in the container')
    parser.add_argument('--container', default='glob2-sprite-render')
    parser.add_argument('--container-work', default='/work')
    parser.add_argument('--blender', default='/work/blender-2.34-linux-glibc2.2.5-i386-static/blender')
    parser.add_argument('--size', type=int, default=40, help='render: square output size (40 native, 128 HD)')
    parser.add_argument('--sets', nargs='*', help='render: only these sets')
    parser.add_argument('--workers', type=int, default=4, help='render: parallel Blender processes')
    args = parser.parse_args()
    renderer = Renderer(args.work, args.container, args.container_work, args.blender)
    {'build': build, 'render': render_sets}[args.command](args, renderer)


if __name__ == '__main__':
    main()
