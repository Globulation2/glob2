/* Indexed geometry and camera metadata are validated before use. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { decodeRig, evaluateRig, type SkinModel } from './rig.ts';
export type Mesh = {
  rig?: SkinModel;
  clip?: number;
  count: number;
  frames: number;
  uv: Float32Array;
  indices: Uint32Array;
  poses: Float32Array;
};
export type ViewTransform = {
  version: number;
  meshSha256: string;
  clipToModel: number[];
  modelToClip: number[];
  normalToModel: number[];
  pivot: number[];
  radius: number;
};
export type Camera = { yaw: number; zoom: number; game: boolean; angle: number };
export const DEFAULT_CAMERA: Camera = { yaw: 0, zoom: 1, game: false, angle: 0 };
export const ACTIONS = {
  worker: ['walk', 'swim', 'harvest'],
  warrior: ['walk', 'swim', 'fight'],
  explorer: ['fly'],
  swarm: [''],
};
export function decode(bytes: ArrayBuffer): Mesh {
  if (bytes.byteLength < 20 || bytes.byteLength > 64 * 1024 * 1024)
    throw new Error('Invalid model size');
  const d = new DataView(bytes),
    count = d.getUint32(4, true),
    indices = d.getUint32(8, true),
    frames = d.getUint32(12, true);
  if (
    d.getUint32(0, true) !== 0x314b5347 ||
    count < 3 ||
    count > 8192 ||
    indices < 3 ||
    indices > 49152 ||
    indices % 3 ||
    ![1, 256].includes(frames) ||
    bytes.byteLength !== 20 + count * 8 + indices * 4 + frames * count * 24
  )
    throw new Error('Invalid model dimensions');
  const uv = new Float32Array(bytes, 20, count * 2),
    index = new Uint32Array(bytes, 20 + count * 8, indices),
    poses = new Float32Array(bytes, 20 + count * 8 + indices * 4);
  if (
    index.some((v) => v >= count) ||
    uv.some((v) => !Number.isFinite(v) || v < 0 || v > 1) ||
    poses.some((v) => !Number.isFinite(v))
  )
    throw new Error('Invalid model geometry');
  return { count, frames, uv, indices: index, poses };
}
export function validateView(view: ViewTransform) {
  if (
    view.version !== 1 ||
    !/^[a-f0-9]{64}$/.test(view.meshSha256) ||
    !Number.isFinite(view.radius) ||
    view.radius <= 0
  )
    throw new Error('Invalid model camera');
  for (const [values, length] of [
    [view.clipToModel, 16],
    [view.modelToClip, 16],
    [view.normalToModel, 9],
    [view.pivot, 3],
  ] as const)
    if (
      !Array.isArray(values) ||
      values.length !== length ||
      values.some((v) => !Number.isFinite(v))
    )
      throw new Error('Invalid model transform');
}
const cache = new Map<string, Promise<{ mesh: Mesh; view: ViewTransform }>>();
// Internal migration switch. Assets that have not passed their acceptance
// gates continue loading their baked counterpart, including missing rig files.
const rigPreview = import.meta.env.VITE_SKIN_RIGS === '1';
const rigCandidates = new Set([
  'worker-walk',
  'warrior-walk',
  'warrior-swim',
  'warrior-fight',
  'explorer-fly',
]);
export async function loadRigMesh(asset: string, clip = 0) {
  const response = await fetch(`/skins/models/${asset}.gsr`);
  if (!response.ok) throw new Error('Could not load rig');
  const bytes = await response.arrayBuffer();
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
  return rigGeometry(decodeRig(bytes), clip, hash);
}
export function rigGeometry(
  rig: SkinModel,
  clip: number,
  meshSha256: string,
): { mesh: Mesh; view: ViewTransform } {
  const camera = rig.clips[clip];
  if (!camera || !Number.isInteger(clip)) throw new Error('Invalid rig clip');
  const m = camera.modelToClip;
  const determinant =
    m[0]! * (m[5]! * m[10]! - m[6]! * m[9]!) -
    m[1]! * (m[4]! * m[10]! - m[6]! * m[8]!) +
    m[2]! * (m[4]! * m[9]! - m[5]! * m[8]!);
  const inverse = [
    m[5]! * m[10]! - m[6]! * m[9]!,
    m[2]! * m[9]! - m[1]! * m[10]!,
    m[1]! * m[6]! - m[2]! * m[5]!,
    0,
    m[6]! * m[8]! - m[4]! * m[10]!,
    m[0]! * m[10]! - m[2]! * m[8]!,
    m[2]! * m[4]! - m[0]! * m[6]!,
    0,
    m[4]! * m[9]! - m[5]! * m[8]!,
    m[1]! * m[8]! - m[0]! * m[9]!,
    m[0]! * m[5]! - m[1]! * m[4]!,
    0,
    0,
    0,
    0,
    determinant,
  ].map((v) => v / determinant);
  for (let row = 0; row < 3; row++)
    inverse[row * 4 + 3] = -(
      inverse[row * 4]! * m[3]! +
      inverse[row * 4 + 1]! * m[7]! +
      inverse[row * 4 + 2]! * m[11]!
    );
  const normals = camera.normalToCamera;
  const view: ViewTransform = {
    version: 1,
    meshSha256,
    clipToModel: inverse,
    modelToClip: [...m],
    normalToModel: Array.from({ length: 9 }, (_, k) => normals[(k % 3) * 3 + Math.floor(k / 3)]!),
    pivot: [...camera.pivot],
    radius: camera.radius,
  };
  // Preserve the fixed rest chart for fill/pattern tools. Animation is evaluated
  // only for the displayed pose, and the same result drives brush visibility.
  const rest = new Float32Array(rig.count * 6);
  for (let v = 0; v < rig.count; v++) {
    rest.set(point(m, rig.rest[v * 6]!, rig.rest[v * 6 + 1]!, rig.rest[v * 6 + 2]!), v * 6);
    for (let k = 0; k < 3; k++)
      rest[v * 6 + 3 + k] =
        normals[k * 3]! * rig.rest[v * 6 + 3]! +
        normals[k * 3 + 1]! * rig.rest[v * 6 + 4]! +
        normals[k * 3 + 2]! * rig.rest[v * 6 + 5]!;
  }
  return {
    mesh: {
      count: rig.count,
      frames: 256,
      uv: new Float32Array(rig.uv),
      indices: new Uint32Array(rig.indices),
      poses: rest,
      rig,
      clip,
    },
    view,
  };
}
export function loadMesh(asset: string) {
  let pending = cache.get(asset);
  if (!pending) {
    const baked = () =>
      Promise.all([fetch(`/skins/models/${asset}.gsk`), fetch(`/skins/models/${asset}.view.json`)])
        .then(async ([a, b]) => {
          if (!a.ok || !b.ok) throw new Error('Could not load this model. Please retry.');
          const bytes = await a.arrayBuffer(),
            view = (await b.json()) as ViewTransform;
          validateView(view);
          const hash = Array.from(
            new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
            (v) => v.toString(16).padStart(2, '0'),
          ).join('');
          if (hash !== view.meshSha256)
            throw new Error('Model and camera versions do not match. Reload to update.');
          return { mesh: decode(bytes), view };
        })
        .catch((e: unknown) => {
          cache.delete(asset);
          throw e;
        });
    pending = rigPreview && rigCandidates.has(asset) ? loadRigMesh(asset).catch(baked) : baked();
    cache.set(asset, pending);
  }
  return pending;
}
export function point(m: readonly number[], x: number, y: number, z: number): number[] {
  return [
    m[0]! * x + m[1]! * y + m[2]! * z + m[3]!,
    m[4]! * x + m[5]! * y + m[6]! * z + m[7]!,
    m[8]! * x + m[9]! * y + m[10]! * z + m[11]!,
  ];
}
const fitCache = new WeakMap<Mesh, { view: ViewTransform; center: number[]; radius: number }>();
function inspectionFit(mesh: Mesh, view: ViewTransform) {
  const cached = fitCache.get(mesh);
  if (cached?.view === view) return cached;
  const low = [Infinity, Infinity, Infinity],
    high = [-Infinity, -Infinity, -Infinity];
  for (let v = 0; v < mesh.count; v++) {
    const p = point(
      view.clipToModel,
      mesh.poses[v * 6]!,
      mesh.poses[v * 6 + 1]!,
      mesh.poses[v * 6 + 2]!,
    );
    for (let k = 0; k < 3; k++) {
      low[k] = Math.min(low[k]!, p[k]!);
      high[k] = Math.max(high[k]!, p[k]!);
    }
  }
  const fit = {
    view,
    center: low.map((v, i) => (v + high[i]!) / 2),
    radius: Math.max(...high.map((v, i) => v - low[i]!)) * 0.68,
  };
  fitCache.set(mesh, fit);
  return fit;
}
// NDC offset per unit of fur length under the current framing, so fur on the
// studio canvas is as long as on the game's tile (clip space over 1.25).
export function furScale(
  mesh: Mesh,
  view: ViewTransform,
  camera: Camera,
  aspect: number,
): [number, number] {
  let k = 1;
  if (!camera.game) {
    const m = view.modelToClip;
    const game = Math.hypot(m[0]!, m[1]!, m[2]!) / 1.25;
    const { radius } = inspectionFit(mesh, view);
    k = (camera.zoom * 0.82) / radius / game;
  }
  return [k / Math.max(1, aspect), k * Math.min(1, aspect)];
}
export function projectPose(
  mesh: Mesh,
  view: ViewTransform,
  frame: number,
  camera: Camera,
  aspect: number,
): Float32Array {
  const result = new Float32Array(mesh.count * 6);
  const sample = Math.max(0, Math.min(mesh.frames - 1, Math.floor(frame)));
  // Each vertex is fully read before projection overwrites it, so rig evaluation
  // can share the output buffer instead of allocating a second deformed mesh.
  const pose = mesh.rig
    ? evaluateRig({ model: mesh.rig, clip: mesh.clip ?? 0, sample }, true, result)
    : mesh.poses.subarray(sample * mesh.count * 6);
  const inv = view.clipToModel,
    n = view.normalToModel;
  // Framing is fixed from the rest pose, cached across camera and animation updates.
  const { center, radius } = inspectionFit(mesh, view);
  const sy = Math.sin(camera.yaw),
    cy = Math.cos(camera.yaw);
  const ca = Math.cos((-camera.angle * Math.PI) / 180),
    sa = Math.sin((-camera.angle * Math.PI) / 180);
  const factor = (camera.zoom * 0.82) / radius;
  for (let v = 0; v < mesh.count; v++) {
    const i = v * 6;
    const p = point(inv, pose[i]!, pose[i + 1]!, pose[i + 2]!);
    const normal = [0, 1, 2].map(
      (k) => n[k * 3]! * pose[i + 3]! + n[k * 3 + 1]! * pose[i + 4]! + n[k * 3 + 2]! * pose[i + 5]!,
    );
    let position: number[], rotated: number[];
    if (camera.game) {
      const x = p[0]! - view.pivot[0]!,
        y = p[1]! - view.pivot[1]!;
      position = point(
        view.modelToClip,
        ca * x - sa * y + view.pivot[0]!,
        sa * x + ca * y + view.pivot[1]!,
        p[2]!,
      );
      const nx = ca * normal[0]! - sa * normal[1]!,
        ny = sa * normal[0]! + ca * normal[1]!;
      rotated = [0, 1, 2].map((k) => n[k]! * nx + n[k + 3]! * ny + n[k + 6]! * normal[2]!);
      position[0] = position[0]! / 1.25 / Math.max(1, aspect);
      position[1] = (position[1]! / 1.25) * Math.min(1, aspect);
    } else {
      const rotate = (a: number[]) => {
        // Turn around model-space Z (upright), then use the fixed exported
        // camera basis. Rotating projected axes would tilt the model as it turns.
        const x = cy * a[0]! - sy * a[1]!,
          y = sy * a[0]! + cy * a[1]!;
        return [0, 1, 2].map((k) => n[k]! * x + n[k + 3]! * y + n[k + 6]! * a[2]!);
      };
      position = rotate(p.map((v, k) => v - center[k]!));
      position = [
        (position[0]! * factor) / Math.max(1, aspect),
        position[1]! * factor * Math.min(1, aspect),
        -position[2]! / (radius * 4),
      ];
      rotated = rotate(normal);
    }
    result.set([...position, ...rotated], i);
  }
  return result;
}
