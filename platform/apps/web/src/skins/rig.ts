// SPDX-License-Identifier: GPL-3.0-or-later
/* GSR1 contract; keep in agreement with libgag/src/SkinModel.cpp.
 *
 * A bone rig stores a rest mesh whose vertices each follow up to four bones,
 * a parent-ordered skeleton with rest and inverse-bind transforms, and clips
 * of uniformly spaced bone keys. A frame interpolates the keys, turns the
 * posed skeleton about the clip pivot by the frame's heading and skins the
 * rest mesh with the result. Coordinates are right-handed model space;
 * matrices are row-major; quaternions are xyzw; transforms apply positive
 * uniform scale, rotation, then translation. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { normalizeOrUp } from './affine.ts';
import {
  ByteReader,
  CLIP_FRAMES,
  MAX_ASSET_BYTES,
  MAX_HEADING,
  MAX_SCALAR,
  TOLERANCE,
  readClipCamera,
  requireAsset,
  requireGeometryCounts,
  type ClipCamera,
} from './binaryAsset.ts';

type Values = readonly number[];
export type RigTransform = Readonly<{ translation: Values; rotation: Values; scale: number }>;
export type RigBone = Readonly<{ parent: number; rest: RigTransform; inverseBind: RigTransform }>;
export type RigClip = Readonly<
  ClipCamera & {
    id: number;
    samples: number;
    duration: number;
    pivot: Values;
    radius: number;
    frames: readonly Readonly<{ heading: number; time: number }>[];
    /** Sample-major local transforms; the last sample wraps to the first. */
    tracks: readonly RigTransform[];
  }
>;
export type RigModel = Readonly<{
  count: number;
  logicalSize: number;
  rest: Float32Array; // xyz, normal xyz per vertex
  uv: Float32Array;
  indices: Uint32Array;
  bones: readonly RigBone[];
  influences: readonly Readonly<{ bones: Values; weights: Values }>[];
  clips: readonly RigClip[];
}>;
/** Row-major 4x4 position matrices and 3x3 normal matrices per bone. */
export type RigPalette = { positions: number[][]; normals: number[][] };

const NO_PARENT = 0xffffffff;
const HEADER_BYTES = 28;
const MAX_BONES = 32;
const MAX_SAMPLES = 256;
// Serialized sizes: a vertex (position, normal, UV, four bones, four
// weights), a bone (parent, rest, inverse bind), a clip header and a key.
const VERTEX_BYTES = 64;
const BONE_BYTES = 68;
const CLIP_HEADER_BYTES = 2176;
const TRANSFORM_BYTES = 32;
// Endpoints the decoders compare in binary64 after rounding to binary32.
const MINIMUM_SCALE = Math.fround(0.0001);
const MINIMUM_DURATION = Math.fround(0.0001);
// Below this cosine the arc between keys is too short for spherical
// interpolation to differ from linear.
const SLERP_THRESHOLD = 0.9995;
const INVERSE_BIND_TOLERANCE = 0.002;

function matrix(t: RigTransform): number[] {
  const [x, y, z, w] = t.rotation as [number, number, number, number];
  const s = t.scale;
  return [
    (1 - 2 * (y * y + z * z)) * s,
    2 * (x * y - z * w) * s,
    2 * (x * z + y * w) * s,
    t.translation[0]!,
    2 * (x * y + z * w) * s,
    (1 - 2 * (x * x + z * z)) * s,
    2 * (y * z - x * w) * s,
    t.translation[1]!,
    2 * (x * z - y * w) * s,
    2 * (y * z + x * w) * s,
    (1 - 2 * (x * x + y * y)) * s,
    t.translation[2]!,
    0,
    0,
    0,
    1,
  ];
}
function multiply(a: Values, b: Values): number[] {
  const out = new Array<number>(16).fill(0);
  for (let y = 0; y < 4; y++)
    for (let x = 0; x < 4; x++)
      for (let k = 0; k < 4; k++) out[y * 4 + x]! += a[y * 4 + k]! * b[k * 4 + x]!;
  return out;
}
function readTransform(reader: ByteReader): RigTransform {
  const translation = reader.f32s(3),
    rotation = reader.f32s(4),
    scale = reader.f32();
  const norm = rotation.reduce((sum, v) => sum + v * v, 0);
  requireAsset(
    Math.abs(norm - 1) <= TOLERANCE && scale >= MINIMUM_SCALE && scale <= MAX_SCALAR,
    'Invalid rig transform',
  );
  return Object.freeze({
    translation: Object.freeze(translation),
    rotation: Object.freeze(rotation.map((v) => Math.fround(v / Math.sqrt(norm)))),
    scale,
  });
}
function readVertices(reader: ByteReader, count: number, boneCount: number) {
  const rest = new Float32Array(count * 6),
    uv = new Float32Array(count * 2);
  const influences: RigModel['influences'][number][] = [];
  for (let v = 0; v < count; v++) {
    const vertex = reader.f32s(6),
      coords = reader.f32s(2),
      bones = Array.from({ length: 4 }, () => reader.u32()),
      weights = reader.f32s(4);
    requireAsset(
      Math.abs(vertex.slice(3).reduce((sum, n) => sum + n * n, 0) - 1) < TOLERANCE,
      'Invalid rig normal',
    );
    requireAsset(
      coords.every((c) => c >= 0 && c <= 1),
      'Invalid rig UV',
    );
    requireAsset(
      bones.every((b) => b < boneCount),
      'Invalid rig influence bone',
    );
    const sum = weights.reduce((a, b) => a + b, 0);
    requireAsset(
      weights.every((w) => w >= 0 && w <= 1) && Math.abs(sum - 1) < TOLERANCE,
      'Invalid rig weights',
    );
    rest.set(vertex, v * 6);
    uv.set(coords, v * 2);
    influences.push(
      Object.freeze({
        bones: Object.freeze(bones),
        weights: Object.freeze(weights.map((w) => Math.fround(w / sum))),
      }),
    );
  }
  return { rest, uv, influences };
}
function readBones(reader: ByteReader, boneCount: number): RigBone[] {
  const bones: RigBone[] = [],
    global: number[][] = [];
  for (let b = 0; b < boneCount; b++) {
    const parent = reader.u32();
    requireAsset(parent === NO_PARENT || parent < b, 'Invalid rig hierarchy');
    const rest = readTransform(reader),
      inverseBind = readTransform(reader);
    global[b] = parent === NO_PARENT ? matrix(rest) : multiply(global[parent]!, matrix(rest));
    const identity = multiply(global[b]!, matrix(inverseBind));
    requireAsset(
      identity.every(
        (v, i) =>
          Number.isFinite(v) && Math.abs(v - (i % 5 === 0 ? 1 : 0)) < INVERSE_BIND_TOLERANCE,
      ),
      'Invalid rig inverse bind',
    );
    bones.push(Object.freeze({ parent, rest, inverseBind }));
  }
  return bones;
}
function readClip(reader: ByteReader, bones: readonly RigBone[], taken: Set<number>): RigClip {
  const id = reader.u32(),
    samples = reader.u32(),
    duration = reader.f32();
  requireAsset(
    samples >= 1 && samples <= MAX_SAMPLES && duration >= MINIMUM_DURATION,
    'Invalid rig track dimensions',
  );
  requireAsset(!taken.has(id), 'Duplicate rig clip');
  taken.add(id);
  const camera = readClipCamera(reader, 'rig');
  const pivot = reader.f32s(3),
    radius = reader.f32();
  requireAsset(radius > 0, 'Invalid rig radius');
  const frames = Array.from({ length: CLIP_FRAMES }, () => {
    const heading = reader.f32(),
      time = reader.f32();
    requireAsset(
      Math.abs(heading) <= MAX_HEADING && time >= 0 && time < duration,
      'Invalid rig frame mapping',
    );
    return Object.freeze({ heading, time });
  });
  requireAsset(
    samples * bones.length * TRANSFORM_BYTES <= reader.remaining,
    'Truncated rig tracks',
  );
  const tracks = Array.from({ length: samples * bones.length }, () => readTransform(reader));
  // Bound every interpolated hierarchy, including combinations between keys:
  // a bone's scale range times its ancestors' and its inverse bind must stay
  // representable.
  const lo: number[] = [],
    hi: number[] = [];
  bones.forEach((bone, b) => {
    const scales = Array.from({ length: samples }, (_, i) => tracks[i * bones.length + b]!.scale);
    const parentLo = bone.parent === NO_PARENT ? 1 : lo[bone.parent]!,
      parentHi = bone.parent === NO_PARENT ? 1 : hi[bone.parent]!;
    lo[b] = Math.min(...scales) * parentLo;
    hi[b] = Math.max(...scales) * parentHi;
    const inverse = bone.inverseBind.scale;
    requireAsset(
      lo[b]! >= MINIMUM_SCALE &&
        hi[b]! <= MAX_SCALAR &&
        lo[b]! * inverse >= MINIMUM_SCALE &&
        hi[b]! * inverse <= MAX_SCALAR,
      'Unbounded rig scale',
    );
  });
  return Object.freeze({
    id,
    samples,
    duration,
    ...camera,
    pivot: Object.freeze(pivot),
    radius,
    frames: Object.freeze(frames),
    tracks: Object.freeze(tracks),
  });
}
export function decodeRig(bytes: ArrayBuffer): RigModel {
  requireAsset(
    bytes.byteLength >= HEADER_BYTES && bytes.byteLength <= MAX_ASSET_BYTES,
    'Invalid rig size',
  );
  const reader = new ByteReader(bytes, 'rig');
  requireAsset(reader.u32() === 0x31525347, 'Unsupported rig format');
  const count = reader.u32(),
    indexCount = reader.u32(),
    boneCount = reader.u32(),
    clipCount = reader.u32(),
    logicalSize = reader.u32(),
    payload = reader.u32();
  requireGeometryCounts(count, indexCount, clipCount, logicalSize, 'rig');
  requireAsset(
    boneCount >= 1 && boneCount <= MAX_BONES && payload === bytes.byteLength - HEADER_BYTES,
    'Invalid rig dimensions',
  );
  // Fixed geometry and every clip header must fit before allocating.
  requireAsset(
    HEADER_BYTES +
      count * VERTEX_BYTES +
      indexCount * 4 +
      boneCount * BONE_BYTES +
      clipCount * CLIP_HEADER_BYTES <=
      bytes.byteLength,
    'Truncated rig geometry',
  );
  const { rest, uv, influences } = readVertices(reader, count, boneCount);
  const indices = new Uint32Array(indexCount);
  for (let i = 0; i < indexCount; i++) {
    indices[i] = reader.u32();
    requireAsset(indices[i]! < count, 'Invalid rig index');
  }
  const bones = readBones(reader, boneCount);
  const taken = new Set<number>();
  const clips = Array.from({ length: clipCount }, () => readClip(reader, bones, taken));
  reader.assertConsumed();
  return Object.freeze({
    count,
    logicalSize,
    rest,
    uv,
    indices,
    influences: Object.freeze(influences),
    bones: Object.freeze(bones),
    clips: Object.freeze(clips),
  });
}
/** Linear translation and scale; shortest-arc spherical rotation. */
function interpolate(a: RigTransform, b: RigTransform, f: number): RigTransform {
  const dot = a.rotation.reduce((sum, v, i) => sum + v * b.rotation[i]!, 0),
    sign = dot < 0 ? -1 : 1;
  const absolute = Math.min(1, Math.abs(dot));
  let left = 1 - f,
    right = f;
  if (absolute < SLERP_THRESHOLD) {
    const angle = Math.acos(absolute),
      divisor = Math.sin(angle);
    left = Math.sin((1 - f) * angle) / divisor;
    right = Math.sin(f * angle) / divisor;
  }
  const rotation = a.rotation.map((v, i) => left * v + right * sign * b.rotation[i]!);
  const norm = Math.hypot(...rotation);
  return {
    translation: a.translation.map((v, i) => v * (1 - f) + b.translation[i]! * f),
    rotation: rotation.map((v) => v / norm),
    scale: a.scale * (1 - f) + b.scale * f,
  };
}
/** Bone matrices at a continuous time (wrapping in either direction) and
 * heading. Gameplay frames go through paletteAtFrame. */
export function paletteAtTime(
  model: RigModel,
  clipIndex: number,
  time: number,
  heading: number,
): RigPalette {
  const clip = model.clips[clipIndex];
  requireAsset(
    Number.isInteger(clipIndex) && !!clip && Number.isFinite(time) && Number.isFinite(heading),
    'Invalid rig pose',
  );
  let wrapped = time % clip.duration;
  if (wrapped < 0) wrapped += clip.duration;
  const sample = (wrapped / clip.duration) * clip.samples,
    a = Math.min(Math.floor(sample), clip.samples - 1),
    b = (a + 1) % clip.samples,
    f = sample - a;
  // Heading turns the posed model about the clip pivot's vertical axis.
  const c = Math.cos(heading),
    s = Math.sin(heading),
    [x, y] = clip.pivot as [number, number, number];
  const turn = [c, -s, 0, x - c * x + s * y, s, c, 0, y - s * x - c * y, 0, 0, 1, 0, 0, 0, 0, 1];
  const global: number[][] = [],
    positions: number[][] = [],
    normals: number[][] = [];
  model.bones.forEach((bone, i) => {
    const local = matrix(
      interpolate(
        clip.tracks[a * model.bones.length + i]!,
        clip.tracks[b * model.bones.length + i]!,
        f,
      ),
    );
    global[i] = bone.parent === NO_PARENT ? local : multiply(global[bone.parent]!, local);
    const m = multiply(turn, multiply(global[i]!, matrix(bone.inverseBind)));
    positions.push(m);
    // Uniform scale: the normal matrix is the rotation divided by the scale.
    const scaleSquared = m[0]! ** 2 + m[4]! ** 2 + m[8]! ** 2;
    normals.push(
      Array.from({ length: 9 }, (_, k) => m[Math.floor(k / 3) * 4 + (k % 3)]! / scaleSquared),
    );
  });
  return { positions, normals };
}
/** Bone matrices for one of the 256 gameplay frames of a clip. */
export function paletteAtFrame(model: RigModel, clip: number, frame: number): RigPalette {
  requireAsset(Number.isInteger(frame) && frame >= 0 && frame < CLIP_FRAMES, 'Invalid rig sample');
  const mapping = model.clips[clip]?.frames[frame];
  requireAsset(!!mapping, 'Invalid rig clip');
  return paletteAtTime(model, clip, mapping.time, mapping.heading);
}
/** Deforms the rest mesh for one frame as xyz, normal xyz per vertex: in
 * camera (clip) space by default, or in model space with the heading
 * applied. Reuses `output` when it has the right length; the vertex loop
 * allocates nothing. */
export function evaluateRig(
  model: RigModel,
  clip: number,
  frame: number,
  cameraSpace = true,
  output?: Float32Array,
): Float32Array {
  const palette = paletteAtFrame(model, clip, frame),
    camera = model.clips[clip]!;
  const out = output?.length === model.count * 6 ? output : new Float32Array(model.count * 6);
  const view = camera.modelToClip,
    normalView = camera.normalToCamera;
  for (let v = 0; v < model.count; v++) {
    const offset = v * 6;
    const x = model.rest[offset]!,
      y = model.rest[offset + 1]!,
      z = model.rest[offset + 2]!;
    const rx = model.rest[offset + 3]!,
      ry = model.rest[offset + 4]!,
      rz = model.rest[offset + 5]!;
    let px = 0,
      py = 0,
      pz = 0,
      nx = 0,
      ny = 0,
      nz = 0;
    const influences = model.influences[v]!;
    for (let influence = 0; influence < 4; influence++) {
      const bone = influences.bones[influence]!,
        weight = influences.weights[influence]!;
      const m = palette.positions[bone]!,
        n = palette.normals[bone]!;
      px += weight * (m[0]! * x + m[1]! * y + m[2]! * z + m[3]!);
      py += weight * (m[4]! * x + m[5]! * y + m[6]! * z + m[7]!);
      pz += weight * (m[8]! * x + m[9]! * y + m[10]! * z + m[11]!);
      nx += weight * (n[0]! * rx + n[1]! * ry + n[2]! * rz);
      ny += weight * (n[3]! * rx + n[4]! * ry + n[5]! * rz);
      nz += weight * (n[6]! * rx + n[7]! * ry + n[8]! * rz);
    }
    // Opposing influences can cancel a normal. Normalizing before and after
    // the camera matches the native evaluator and the shader.
    out[offset + 3] = nx;
    out[offset + 4] = ny;
    out[offset + 5] = nz;
    normalizeOrUp(out, offset + 3);
    if (cameraSpace) {
      out[offset] = view[0]! * px + view[1]! * py + view[2]! * pz + view[3]!;
      out[offset + 1] = view[4]! * px + view[5]! * py + view[6]! * pz + view[7]!;
      out[offset + 2] = view[8]! * px + view[9]! * py + view[10]! * pz + view[11]!;
      nx = out[offset + 3]!;
      ny = out[offset + 4]!;
      nz = out[offset + 5]!;
      out[offset + 3] = normalView[0]! * nx + normalView[1]! * ny + normalView[2]! * nz;
      out[offset + 4] = normalView[3]! * nx + normalView[4]! * ny + normalView[5]! * nz;
      out[offset + 5] = normalView[6]! * nx + normalView[7]! * ny + normalView[8]! * nz;
      normalizeOrUp(out, offset + 3);
    } else {
      out[offset] = px;
      out[offset + 1] = py;
      out[offset + 2] = pz;
    }
  }
  return out;
}
