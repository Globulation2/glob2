// SPDX-License-Identifier: GPL-3.0-or-later
/* GSR1 contract; keep in agreement with libgag/src/SkinModel.cpp. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
type Values = readonly number[];
export type RigTransform = Readonly<{ translation: Values; rotation: Values; scale: number }>;
export type RigBone = Readonly<{ parent: number; rest: RigTransform; inverseBind: RigTransform }>;
export type RigClip = Readonly<{
  id: number;
  samples: number;
  duration: number;
  modelToClip: Values;
  normalToCamera: Values;
  pivot: Values;
  radius: number;
  frames: readonly Readonly<{ heading: number; time: number }>[];
  tracks: readonly RigTransform[];
}>;
export type SkinModel = Readonly<{
  count: number;
  logicalSize: number;
  rest: Values;
  uv: Values;
  indices: Values;
  bones: readonly RigBone[];
  influences: readonly Readonly<{ bones: Values; weights: Values }>[];
  clips: readonly RigClip[];
}>;
export type SkinPoseRequest = Readonly<{ model: SkinModel; clip: number; sample: number }>;
export type SkinPalette = { positions: number[][]; normals: number[][] };
const noParent = 0xffffffff;
// Serialized endpoint limits are binary32; all validation calculations use
// binary64, matching SkinModel::decode. Normalized storage remains binary32.
const tolerance = 0.0001;
const minimumScale = Math.fround(0.0001);
const minimumDuration = Math.fround(0.0001);
const maximumHeading = Math.fround(6.283186);
function requireRig(ok: boolean, message: string): asserts ok {
  if (!ok) throw new Error(message);
}
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
export function decodeRig(bytes: ArrayBuffer): SkinModel {
  requireRig(bytes.byteLength >= 28 && bytes.byteLength <= 16 * 1024 * 1024, 'Invalid rig size');
  const data = new DataView(bytes);
  let at = 0;
  const word = () => {
    requireRig(at + 4 <= bytes.byteLength, 'Truncated rig payload');
    const value = data.getUint32(at, true);
    at += 4;
    return value;
  };
  const scalar = () => {
    requireRig(at + 4 <= bytes.byteLength, 'Truncated rig payload');
    const value = data.getFloat32(at, true);
    at += 4;
    requireRig(Number.isFinite(value) && Math.abs(value) <= 10000, 'Invalid rig scalar');
    return value;
  };
  const values = (count: number) => Array.from({ length: count }, scalar);
  const transform = (): RigTransform => {
    const translation = values(3),
      rotation = values(4),
      scale = scalar();
    const norm = rotation.reduce((sum, v) => sum + v * v, 0);
    requireRig(
      Math.abs(norm - 1) <= tolerance && scale >= minimumScale && scale <= 10000,
      'Invalid rig transform',
    );
    return Object.freeze({
      translation: Object.freeze(translation),
      rotation: Object.freeze(rotation.map((v) => Math.fround(v / Math.sqrt(norm)))),
      scale,
    });
  };
  requireRig(word() === 0x31525347, 'Unsupported rig format');
  const count = word(),
    indexCount = word(),
    boneCount = word(),
    clipCount = word(),
    logicalSize = word(),
    payload = word();
  requireRig(
    count >= 3 &&
      count <= 8192 &&
      indexCount >= 3 &&
      indexCount <= 49152 &&
      indexCount % 3 === 0 &&
      boneCount >= 1 &&
      boneCount <= 32 &&
      clipCount >= 1 &&
      clipCount <= 8 &&
      logicalSize >= 1 &&
      logicalSize <= 128 &&
      payload === bytes.byteLength - 28,
    'Invalid rig dimensions',
  );
  requireRig(
    28 + count * 64 + indexCount * 4 + boneCount * 68 + clipCount * 2176 <= bytes.byteLength,
    'Truncated rig geometry',
  );
  const rest: number[] = [],
    uv: number[] = [];
  const influences: SkinModel['influences'][number][] = [];
  for (let v = 0; v < count; v++) {
    const vertex = values(6),
      coords = values(2),
      bones = Array.from({ length: 4 }, word),
      weights = values(4);
    requireRig(
      Math.abs(vertex.slice(3).reduce((sum, n) => sum + n * n, 0) - 1) < tolerance,
      'Invalid rig normal',
    );
    requireRig(
      coords.every((v) => v >= 0 && v <= 1),
      'Invalid rig UV',
    );
    requireRig(
      bones.every((b) => b < boneCount),
      'Invalid rig influence bone',
    );
    const sum = weights.reduce((a, b) => a + b, 0);
    requireRig(
      weights.every((w) => w >= 0 && w <= 1) && Math.abs(sum - 1) < tolerance,
      'Invalid rig weights',
    );
    rest.push(...vertex);
    uv.push(...coords);
    influences.push(
      Object.freeze({
        bones: Object.freeze(bones),
        weights: Object.freeze(weights.map((w) => Math.fround(w / sum))),
      }),
    );
  }
  const indices = Array.from({ length: indexCount }, word);
  requireRig(
    indices.every((i) => i < count),
    'Invalid rig index',
  );
  const bones: RigBone[] = [],
    global: number[][] = [];
  for (let b = 0; b < boneCount; b++) {
    const parent = word();
    requireRig(parent === noParent || parent < b, 'Invalid rig hierarchy');
    const rest = transform(),
      inverseBind = transform();
    global[b] = parent === noParent ? matrix(rest) : multiply(global[parent]!, matrix(rest));
    const identity = multiply(global[b]!, matrix(inverseBind));
    requireRig(
      identity.every((v, i) => Number.isFinite(v) && Math.abs(v - (i % 5 === 0 ? 1 : 0)) < 0.002),
      'Invalid rig inverse bind',
    );
    bones.push(Object.freeze({ parent, rest, inverseBind }));
  }
  const clips: RigClip[] = [];
  for (let c = 0; c < clipCount; c++) {
    const id = word(),
      samples = word(),
      duration = scalar();
    requireRig(
      samples >= 1 && samples <= 256 && duration >= minimumDuration,
      'Invalid rig track dimensions',
    );
    requireRig(!clips.some((c) => c.id === id), 'Duplicate rig clip');
    const modelToClip = values(16),
      normalToCamera = values(9),
      pivot = values(3),
      radius = scalar();
    requireRig(radius > 0, 'Invalid rig radius');
    const m = modelToClip,
      n = normalToCamera;
    requireRig(m[12] === 0 && m[13] === 0 && m[14] === 0 && m[15] === 1, 'Invalid rig camera');
    const determinant =
      m[0]! * (m[5]! * m[10]! - m[6]! * m[9]!) -
      m[1]! * (m[4]! * m[10]! - m[6]! * m[8]!) +
      m[2]! * (m[4]! * m[9]! - m[5]! * m[8]!);
    requireRig(Math.abs(determinant) > 1e-12, 'Singular rig camera');
    for (let i = 0; i < 3; i++)
      for (let j = 0; j < 3; j++) {
        let dot = 0;
        for (let k = 0; k < 3; k++) dot += n[i * 3 + k]! * n[j * 3 + k]!;
        requireRig(Math.abs(dot - (i === j ? 1 : 0)) < tolerance, 'Invalid rig normal camera');
      }
    const frames = Array.from({ length: 256 }, () => {
      const heading = scalar(),
        time = scalar();
      requireRig(
        Math.abs(heading) <= maximumHeading && time >= 0 && time < duration,
        'Invalid rig frame mapping',
      );
      return Object.freeze({ heading, time });
    });
    requireRig(samples * boneCount * 32 <= bytes.byteLength - at, 'Truncated rig tracks');
    const tracks = Array.from({ length: samples * boneCount }, transform);
    const lo: number[] = [],
      hi: number[] = [];
    for (let b = 0; b < boneCount; b++) {
      const scales = Array.from({ length: samples }, (_, i) => tracks[i * boneCount + b]!.scale);
      const parent = bones[b]!.parent;
      lo[b] = Math.min(...scales) * (parent === noParent ? 1 : lo[parent]!);
      hi[b] = Math.max(...scales) * (parent === noParent ? 1 : hi[parent]!);
      const inverse = bones[b]!.inverseBind.scale;
      requireRig(
        lo[b]! >= minimumScale &&
          hi[b]! <= 10000 &&
          lo[b]! * inverse >= minimumScale &&
          hi[b]! * inverse <= 10000,
        'Unbounded rig scale',
      );
    }
    clips.push(
      Object.freeze({
        id,
        samples,
        duration,
        modelToClip: Object.freeze(modelToClip),
        normalToCamera: Object.freeze(normalToCamera),
        pivot: Object.freeze(pivot),
        radius,
        frames: Object.freeze(frames),
        tracks: Object.freeze(tracks),
      }),
    );
  }
  requireRig(at === bytes.byteLength, 'Rig payload length mismatch');
  return Object.freeze({
    count,
    logicalSize,
    rest: Object.freeze(rest),
    uv: Object.freeze(uv),
    indices: Object.freeze(indices),
    influences: Object.freeze(influences),
    bones: Object.freeze(bones),
    clips: Object.freeze(clips),
  });
}
function interpolate(a: RigTransform, b: RigTransform, f: number): RigTransform {
  const dot = a.rotation.reduce((sum, v, i) => sum + v * b.rotation[i]!, 0),
    sign = dot < 0 ? -1 : 1;
  const absolute = Math.min(1, Math.abs(dot));
  let left = 1 - f,
    right = f;
  if (absolute < 0.9995) {
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
export function rigPalette(
  model: SkinModel,
  clipIndex: number,
  time: number,
  heading: number,
): SkinPalette {
  const clip = model.clips[clipIndex];
  requireRig(
    Number.isInteger(clipIndex) && !!clip && Number.isFinite(time) && Number.isFinite(heading),
    'Invalid rig pose',
  );
  let wrapped = time % clip.duration;
  if (wrapped < 0) wrapped += clip.duration;
  const sample = (wrapped / clip.duration) * clip.samples,
    a = Math.min(Math.floor(sample), clip.samples - 1),
    b = (a + 1) % clip.samples,
    f = sample - a;
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
    global[i] = bone.parent === noParent ? local : multiply(global[bone.parent]!, local);
    const m = multiply(turn, multiply(global[i]!, matrix(bone.inverseBind)));
    positions.push(m);
    const scaleSquared = m[0]! ** 2 + m[4]! ** 2 + m[8]! ** 2;
    normals.push(
      Array.from({ length: 9 }, (_, k) => m[Math.floor(k / 3) * 4 + (k % 3)]! / scaleSquared),
    );
  });
  return { positions, normals };
}
export function rigFramePalette({ model, clip, sample }: SkinPoseRequest): SkinPalette {
  requireRig(Number.isInteger(sample) && sample >= 0 && sample < 256, 'Invalid rig sample');
  const frame = model.clips[clip]?.frames[sample];
  requireRig(!!frame, 'Invalid rig clip');
  return rigPalette(model, clip, frame.time, frame.heading);
}
/** Deform into reusable caller storage. The vertex loop allocates no objects. */
export function evaluateRig(
  request: SkinPoseRequest,
  cameraSpace = true,
  output?: Float32Array,
): Float32Array {
  const { model, clip } = request;
  const palette = rigFramePalette(request),
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
    // Opposing influences can cancel a normal. The fallback and both
    // normalization steps intentionally match the native evaluator and shader.
    let length = Math.hypot(nx, ny, nz);
    if (length < 1e-8) {
      nx = 0;
      ny = 0;
      nz = 1;
    } else {
      nx /= length;
      ny /= length;
      nz /= length;
    }
    if (cameraSpace) {
      out[offset] = view[0]! * px + view[1]! * py + view[2]! * pz + view[3]!;
      out[offset + 1] = view[4]! * px + view[5]! * py + view[6]! * pz + view[7]!;
      out[offset + 2] = view[8]! * px + view[9]! * py + view[10]! * pz + view[11]!;
      const tx = normalView[0]! * nx + normalView[1]! * ny + normalView[2]! * nz;
      const ty = normalView[3]! * nx + normalView[4]! * ny + normalView[5]! * nz;
      nz = normalView[6]! * nx + normalView[7]! * ny + normalView[8]! * nz;
      nx = tx;
      ny = ty;
    } else {
      out[offset] = px;
      out[offset + 1] = py;
      out[offset + 2] = pz;
    }
    length = Math.hypot(nx, ny, nz);
    out[offset + 3] = length < 1e-8 ? 0 : nx / length;
    out[offset + 4] = length < 1e-8 ? 0 : ny / length;
    out[offset + 5] = length < 1e-8 ? 1 : nz / length;
  }
  return out;
}
