# SPDX-License-Identifier: GPL-3.0-or-later
"""Design spec for the *wizard* concept unit, read by derive_unit.py.

The wizard is a proposed fourth unit type: a ranged attacker that will use a
magic spell (the spell effect itself is not designed yet). Its art is derived
from the warrior rig, keeping the glob family's skin, lighting and camera, and
changes three things:

1. **Shape.** The central body metaball becomes a convex, pointed oval of glob
   skin, pointed at the top *and* the bottom. Globs are symmetric, and the walk
   is a 180-degree front flip that loops, which only looks seamless if the
   body is identical upside down. (An earlier hat-like flare on top was
   rejected: the point must be the skin itself, not a hat.)
2. **Limbs.** Arms and legs are long, thin and worker-like rather than the
   warrior's heavy balls: wizards don't do physical work.
3. **Animation.** The warrior's pound becomes a *cast* (arms come together,
   squeeze, release). The swim gets its own stroke: the warrior's swim folds
   each forearm about 165 degrees so the limbs tuck into one lump, which made
   the thin limbs tangle. The walk keeps the warrior's keys unchanged.

Coordinates (see derive_unit.py for the rig facts):
* body elements are in body-local space: +y up, -x forward, z sideways;
* limb directions are in armature space: +x the glob's right, +y up, -z
  forward (standing). For the swim the body is laid flat (RotX 0), so there
  +y is forward and +z is up;
* frames are scene frames of one 8-frame cycle (1..9; frame 9 == frame 1).
"""
from derive_unit import mirror_x


def convex_tip(top, start, r_start, r_end, count, power):
    """Metaball chain from `start` to `top` along +y whose radius falls from
    r_start to r_end along a convex profile (r = r_end + span * (1 - t^power)),
    giving an oval that narrows smoothly into a point rather than a flare."""
    chain = []
    for i in range(count):
        t = i / (count - 1)
        chain.append([0.0, start + (top - start) * t, 0.0, r_end + (r_start - r_end) * (1 - t ** power), 2.0])
    return chain


def point_symmetric(chain):
    """Add the mirror image through the body centre, (x, y) -> (-x, -y): a
    180-degree turn about the walk's flip axis maps the shape onto itself."""
    return chain + [[-x, -y, z, r, s] for x, y, z, r, s in chain]


def ball(x, y, z, radius, stiffness=2.0):
    """Full element record: position, radius, stiffness, type 0 (ball), unit scale."""
    return [x, y, z, radius, stiffness, 0, 1.0, 1.0, 1.0]


# Tips at +-2.8 body units (x3.12 body scale): about 1.8:1 height to width.
# The body's own ball (radius 2) becomes an upright ellipsoid (type 6).
BODY = dict(
    elements=[ball(*e) for e in point_symmetric(convex_tip(top=2.8, start=0.8, r_start=1.45,
                                                           r_end=0.32, count=7, power=1.4))],
    own_element=[('type', 6), ('expx', 0.8), ('expy', 1.2), ('expz', 0.8)],
)

# Warrior: hands/feet 2.4, elbows/knees 1.0 spheres. Thinner elements need to
# be stretched (3.4 along the limb) to stay connected; reaching further than
# 1.2x detaches the hands from the strands.
LIMBS = dict(hand=1.3, limb=(3.4, 0.85, 0.85), reach=1.2, elbow_reach=1.2)


def both_arms(right):
    return {'Bone.004': right, 'Bone.006': [[f, mirror_x(d)] for f, d in right]}


def both_legs(right):
    return {'Bone.002': right, 'Bone': [[f, mirror_x(d)] for f, d in right]}


# Cast: rest with arms out -> hands meet in front (f3) -> squeeze (f4.5, body
# dips) -> release up and out (f6) -> rest. In the 45-degree game camera a
# hand moving toward the viewer drops on screen, so the gather has as much
# "up" as "forward" to keep the hands at mid-body. Legs keep the warrior keys.
CAST = dict(
    limbs=both_arms([[1, [0.9, 0.15, -0.4]], [3, [0.2, 0.75, -0.63]], [4.5, [0.05, 0.75, -0.65]],
                     [6, [0.75, 0.55, -0.35]], [9, [0.9, 0.15, -0.4]]]),
    body={'LocY': [[1, -0.117], [4, 0.3], [6, -0.4], [9, -0.117]],      # slight lean in, recoil
          'LocZ': [[1, 1.049], [4, 0.65], [6, 1.45], [9, 1.049]]},      # dip on squeeze, rise on release
)

# Swim: body flat with the point leading (armature RotX 90 -> 0 degrees); a
# breaststroke with straight arms kept in the body plane (no "periscope" arm
# above the back), and a frog kick peaking with the arm pull, then a surge.
SWIM = dict(
    limbs=dict(**both_arms([[1, [0.45, 0.89, 0.0]], [3, [0.93, 0.37, 0.0]], [5, [0.5, -0.87, 0.0]],
                            [7, [0.3, 0.95, 0.0]], [9, [0.45, 0.89, 0.0]]]),
               **both_legs([[1, [0.38, -0.92, 0.0]], [3, [0.42, -0.9, 0.0]], [5, [0.93, -0.37, 0.0]],
                            [7, [0.2, -0.98, 0.0]], [9, [0.38, -0.92, 0.0]]])),
    body={'RotX': [[1, 0.0], [9, 0.0]],
          'LocY': [[1, -0.117], [4, -0.117], [6, 0.5], [9, -0.117]]},
)

UNIT = dict(
    body=BODY,
    limbs=LIMBS,
    sets=[
        # name, original source, shadow layer (as the shipped set), animation edits
        dict(name='wizard-walk', source='glob-warrior-walk.blend', shadow=True, animation=None),
        dict(name='wizard-swim', source='glob-warrior-swim.blend', shadow=False, animation=SWIM),
        dict(name='wizard-cast', source='glob-warrior-fight.blend', shadow=True, animation=CAST),
    ],
)
