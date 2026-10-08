/* Unit meshes for Studio: baked GSK1 clips, GSB1 blend-shape clips and GSR1
 * bone rigs, all presented as one Mesh with a rest pose for the fixed paint
 * chart and a pose() evaluator for the displayed frame. Indexed geometry and
 * camera metadata are validated before use. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import {
  invertAffine,
  sha256Hex,
  transformNormal,
  transformPoint,
  transposed3,
  type Mat3,
  type Mat4,
} from './affine.ts';
import { CLIP_FRAMES, MAX_INDICES, MAX_VERTICES } from './binaryAsset.ts';
import { decodeRig, evaluateRig, type RigModel } from './rig.ts';
import { decodeShapes, evaluateShapes, type ShapeModel } from './shapes.ts';
export type Mesh = {
  count: number;
  frames: number;
  uv: Float32Array;
  /** Separate procedural unwrap; omitted assets use the paint chart. */
  detailUV?: Float32Array;
  indices: Uint32Array;
  /** xyz, normal xyz per vertex in clip space: the fixed chart for fill and
   * pattern tools, and the framing reference for the inspection camera. */
  rest: Float32Array;
  /** The displayed frame in the same layout. Rigs and blend shapes evaluate
   * into `out` when it has the right length; baked clips return a view. */
  pose(frame: number, out?: Float32Array): Float32Array;
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
/** `yaw` turns the model about its upright axis; `pitch` raises or lowers the
 * inspection camera from the exported game elevation (about 45 degrees). */
export type Camera = { yaw: number; pitch: number; zoom: number; game: boolean; angle: number };
export const DEFAULT_CAMERA: Camera = { yaw: 0, pitch: 0, zoom: 1, game: false, angle: 0 };
/** Inspection tilt stops equally far above and below the exported game view. */
export const MAX_PITCH = (40 * Math.PI) / 180;
export const MIN_PITCH = -MAX_PITCH;
export function clampPitch(pitch: number): number {
  return Math.max(MIN_PITCH, Math.min(MAX_PITCH, pitch));
}
export const ACTIONS = {
  worker: ['walk', 'swim', 'harvest'],
  warrior: ['walk', 'swim', 'fight'],
  explorer: ['fly'],
  swarm: [''],
};
/** The fitted format each unit clip animates from; swarms stay baked. */
export const UNIT_CLIP_FORMATS = {
  'worker-walk': 'gsb',
  'worker-swim': 'gsb',
  'worker-harvest': 'gsb',
  'warrior-walk': 'gsb',
  'warrior-swim': 'gsb',
  'warrior-fight': 'gsb',
  'explorer-fly': 'gsr',
} as const;
export type UnitClipFormat = (typeof UNIT_CLIP_FORMATS)[keyof typeof UNIT_CLIP_FORMATS];
export function unitClipFormat(asset: string): UnitClipFormat | undefined {
  return Object.hasOwn(UNIT_CLIP_FORMATS, asset)
    ? UNIT_CLIP_FORMATS[asset as keyof typeof UNIT_CLIP_FORMATS]
    : undefined;
}
// Clip space is padded by this factor around the logical sprite tile.
export const ATLAS_PADDING = 1.25;
export { transformPoint as point };

/** A mesh over frame-major baked poses (xyz, normal xyz per vertex). */
export function bakedMesh(
  count: number,
  frames: number,
  uv: Float32Array,
  indices: Uint32Array,
  poses: Float32Array,
): Mesh {
  const stride = count * 6;
  return {
    count,
    frames,
    uv,
    indices,
    rest: poses.subarray(0, stride),
    pose: (frame) => poses.subarray(frame * stride, (frame + 1) * stride),
  };
}
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
    count > MAX_VERTICES ||
    indices < 3 ||
    indices > MAX_INDICES ||
    indices % 3 ||
    ![1, CLIP_FRAMES].includes(frames) ||
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
  return bakedMesh(count, frames, uv, index, poses);
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
/** The view sidecar a fitted asset's clip camera implies, with the asset's
 * own digest as the cache identity. */
function cameraView(
  modelToClip: Mat4,
  normalToCamera: Mat3,
  meshSha256: string,
  pivot: number[],
  radius: number,
): ViewTransform {
  return {
    version: 1,
    meshSha256,
    clipToModel: invertAffine(modelToClip),
    modelToClip: [...modelToClip],
    normalToModel: transposed3(normalToCamera),
    pivot,
    radius,
  };
}
/** Takes a model-space rest mesh into clip space with unit normals. */
function restInClipSpace(
  count: number,
  modelToClip: Mat4,
  normalToCamera: Mat3,
  position: (v: number) => [number, number, number],
  normal: (v: number) => [number, number, number],
): Float32Array {
  const rest = new Float32Array(count * 6);
  for (let v = 0; v < count; v++) {
    rest.set(transformPoint(modelToClip, ...position(v)), v * 6);
    const n = transformNormal(normalToCamera, ...normal(v));
    const length = Math.hypot(...n);
    rest.set(length < 1e-8 ? [0, 0, 1] : n.map((k) => k / length), v * 6 + 3);
  }
  return rest;
}
export function shapeGeometry(
  shapes: ShapeModel,
  clip: number,
  meshSha256: string,
): { mesh: Mesh; view: ViewTransform } {
  const camera = shapes.clips[clip];
  if (!camera || !Number.isInteger(clip)) throw new Error('Invalid shape clip');
  // The framing radius is the mean mesh's extent about the model origin.
  let radius = 0;
  for (let v = 0; v < shapes.count; v++)
    radius = Math.max(
      radius,
      Math.hypot(shapes.mean[v * 3]!, shapes.mean[v * 3 + 1]!, shapes.mean[v * 3 + 2]!),
    );
  const view = cameraView(camera.modelToClip, camera.normalToCamera, meshSha256, [0, 0, 0], radius);
  // The fixed chart is the mean mesh at heading zero.
  const rest = restInClipSpace(
    shapes.count,
    camera.modelToClip,
    camera.normalToCamera,
    (v) => [shapes.mean[v * 3]!, shapes.mean[v * 3 + 1]!, shapes.mean[v * 3 + 2]!],
    (v) => [
      shapes.normalMean[v * 3]!,
      shapes.normalMean[v * 3 + 1]!,
      shapes.normalMean[v * 3 + 2]!,
    ],
  );
  return {
    mesh: {
      count: shapes.count,
      frames: CLIP_FRAMES,
      uv: new Float32Array(shapes.uv),
      indices: new Uint32Array(shapes.indices),
      rest,
      pose: (frame, out) => evaluateShapes(shapes, clip, frame, true, out),
    },
    view,
  };
}
export function rigGeometry(
  rig: RigModel,
  clip: number,
  meshSha256: string,
): { mesh: Mesh; view: ViewTransform } {
  const camera = rig.clips[clip];
  if (!camera || !Number.isInteger(clip)) throw new Error('Invalid rig clip');
  const view = cameraView(
    camera.modelToClip,
    camera.normalToCamera,
    meshSha256,
    [...camera.pivot],
    camera.radius,
  );
  // The fixed chart is the rest mesh; animation only drives the displayed pose.
  const rest = restInClipSpace(
    rig.count,
    camera.modelToClip,
    camera.normalToCamera,
    (v) => [rig.rest[v * 6]!, rig.rest[v * 6 + 1]!, rig.rest[v * 6 + 2]!],
    (v) => [rig.rest[v * 6 + 3]!, rig.rest[v * 6 + 4]!, rig.rest[v * 6 + 5]!],
  );
  return {
    mesh: {
      count: rig.count,
      frames: CLIP_FRAMES,
      uv: new Float32Array(rig.uv),
      indices: new Uint32Array(rig.indices),
      rest,
      pose: (frame, out) => evaluateRig(rig, clip, frame, true, out),
    },
    view,
  };
}
/** Loads a unit clip's fitted asset: blend shapes or the bone rig. */
export async function loadFittedMesh(asset: string, clip = 0) {
  const format = unitClipFormat(asset);
  if (!format) throw new Error(`${asset} has no fitted clip`);
  const response = await fetch(`/skins/models/${asset}.${format}`);
  if (!response.ok) throw new Error(`Could not load ${asset}.${format}`);
  const bytes = await response.arrayBuffer();
  const hash = await sha256Hex(bytes);
  return format === 'gsb'
    ? shapeGeometry(decodeShapes(bytes), clip, hash)
    : rigGeometry(decodeRig(bytes), clip, hash);
}
async function loadBakedMesh(asset: string) {
  const [a, b] = await Promise.all([
    fetch(`/skins/models/${asset}.gsk`),
    fetch(`/skins/models/${asset}.view.json`),
  ]);
  if (!a.ok || !b.ok) throw new Error('Could not load this model. Please retry.');
  const bytes = await a.arrayBuffer(),
    view = (await b.json()) as ViewTransform;
  validateView(view);
  if ((await sha256Hex(bytes)) !== view.meshSha256)
    throw new Error('Model and camera versions do not match. Reload to update.');
  return { mesh: decode(bytes), view };
}
export function decodeDetailUV(bytes: ArrayBuffer, count: number): Float32Array {
  const view = new DataView(bytes);
  if (
    count < 3 ||
    count > MAX_VERTICES ||
    bytes.byteLength !== 8 + count * 8 ||
    String.fromCharCode(...new Uint8Array(bytes, 0, 4)) !== 'GUV1' ||
    view.getUint32(4, true) !== count
  )
    throw new Error('Invalid material unwrap dimensions.');
  const uv = new Float32Array(count * 2);
  for (let i = 0; i < uv.length; i++) {
    const value = view.getFloat32(8 + i * 4, true);
    if (!Number.isFinite(value) || value < 0 || value > 1)
      throw new Error('Invalid material unwrap coordinate.');
    uv[i] = value;
  }
  return uv;
}
async function withDetailUV(asset: string, loaded: { mesh: Mesh; view: ViewTransform }) {
  if (!asset.startsWith('worker-') && !asset.startsWith('warrior-')) return loaded;
  const response = await fetch(`/skins/models/${asset}.guv`);
  if (response.status === 404) return loaded;
  if (!response.ok) throw new Error('Could not load the material unwrap.');
  loaded.mesh.detailUV = decodeDetailUV(await response.arrayBuffer(), loaded.mesh.count);
  return loaded;
}
const cache = new Map<string, Promise<{ mesh: Mesh; view: ViewTransform }>>();
/** Unit clips load their fitted asset and fall back to the baked clip, which
 * shares the paint layout, if the fitted asset is missing or invalid. */
export function loadMesh(asset: string) {
  let pending = cache.get(asset);
  if (!pending) {
    pending = (
      unitClipFormat(asset)
        ? loadFittedMesh(asset).catch((error: unknown) => {
            console.warn(`Fitted ${asset} unavailable, using the baked clip:`, error);
            return loadBakedMesh(asset);
          })
        : loadBakedMesh(asset)
    )
      .then((loaded) => withDetailUV(asset, loaded))
      .catch((e: unknown) => {
        cache.delete(asset);
        throw e;
      });
    cache.set(asset, pending);
  }
  return pending;
}
const fitCache = new WeakMap<Mesh, { view: ViewTransform; center: number[]; radius: number }>();
// Inspection framing: the rest pose fills this share of the canvas at zoom 1.
const INSPECTION_FILL = 0.82;
// The inspection radius is this share of the rest pose's largest extent.
const EXTENT_TO_RADIUS = 0.68;
function inspectionFit(mesh: Mesh, view: ViewTransform) {
  const cached = fitCache.get(mesh);
  if (cached?.view === view) return cached;
  const low = [Infinity, Infinity, Infinity],
    high = [-Infinity, -Infinity, -Infinity];
  for (let v = 0; v < mesh.count; v++) {
    const p = transformPoint(
      view.clipToModel,
      mesh.rest[v * 6]!,
      mesh.rest[v * 6 + 1]!,
      mesh.rest[v * 6 + 2]!,
    );
    for (let k = 0; k < 3; k++) {
      low[k] = Math.min(low[k]!, p[k]!);
      high[k] = Math.max(high[k]!, p[k]!);
    }
  }
  const fit = {
    view,
    center: low.map((v, i) => (v + high[i]!) / 2),
    radius: Math.max(...high.map((v, i) => v - low[i]!)) * EXTENT_TO_RADIUS,
  };
  fitCache.set(mesh, fit);
  return fit;
}
// NDC offset per unit of fur length under the current framing, so fur on the
// studio canvas is as long as on the game's tile (clip space over the padding).
export function furScale(
  mesh: Mesh,
  view: ViewTransform,
  camera: Camera,
  aspect: number,
): [number, number] {
  let k = 1;
  if (!camera.game) {
    const m = view.modelToClip;
    const game = Math.hypot(m[0]!, m[1]!, m[2]!) / ATLAS_PADDING;
    const { radius } = inspectionFit(mesh, view);
    k = (camera.zoom * INSPECTION_FILL) / radius / game;
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
  // Each vertex is fully read before projection overwrites it, so evaluation
  // can share the output buffer instead of allocating a second deformed mesh.
  const pose = mesh.pose(sample, result);
  const inv = view.clipToModel,
    n = view.normalToModel;
  // Framing is fixed from the rest pose, cached across camera and animation updates.
  const { center, radius } = inspectionFit(mesh, view);
  const sy = Math.sin(camera.yaw),
    cy = Math.cos(camera.yaw);
  const pitch = clampPitch(camera.pitch),
    sp = Math.sin(pitch),
    cp = Math.cos(pitch);
  const ca = Math.cos((-camera.angle * Math.PI) / 180),
    sa = Math.sin((-camera.angle * Math.PI) / 180);
  const factor = (camera.zoom * INSPECTION_FILL) / radius;
  for (let v = 0; v < mesh.count; v++) {
    const i = v * 6;
    const p = transformPoint(inv, pose[i]!, pose[i + 1]!, pose[i + 2]!);
    const normal = transformNormal(n, pose[i + 3]!, pose[i + 4]!, pose[i + 5]!);
    let position: number[], rotated: number[];
    if (camera.game) {
      const x = p[0] - view.pivot[0]!,
        y = p[1] - view.pivot[1]!;
      position = transformPoint(
        view.modelToClip,
        ca * x - sa * y + view.pivot[0]!,
        sa * x + ca * y + view.pivot[1]!,
        p[2],
      );
      const nx = ca * normal[0] - sa * normal[1],
        ny = sa * normal[0] + ca * normal[1];
      rotated = [0, 1, 2].map((k) => n[k]! * nx + n[k + 3]! * ny + n[k + 6]! * normal[2]);
      position[0] = position[0]! / ATLAS_PADDING / Math.max(1, aspect);
      position[1] = (position[1]! / ATLAS_PADDING) * Math.min(1, aspect);
    } else {
      const rotate = (a: number[]) => {
        // Turn around model-space Z (upright), then use the fixed exported
        // camera basis. Rotating projected axes would tilt the model as it turns.
        const x = cy * a[0]! - sy * a[1]!,
          y = sy * a[0]! + cy * a[1]!;
        const c = [0, 1, 2].map((k) => n[k]! * x + n[k + 3]! * y + n[k + 6]! * a[2]!);
        // Pitch swings the camera about the screen's horizontal axis, after the
        // turn, so the model keeps its heading and the screen stays level.
        return [c[0]!, cp * c[1]! - sp * c[2]!, sp * c[1]! + cp * c[2]!];
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
