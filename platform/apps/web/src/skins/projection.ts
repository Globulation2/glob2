/* Deterministic CPU projection: the same projected pose is used by WebGL. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { point, type Mesh, type ViewTransform } from './geometry.ts';
export type Projection = {
  x: Float32Array;
  y: Float32Array;
  visible: Uint8Array;
  used: Uint8Array;
  depth: Float32Array;
};
function triangle(
  a: number[],
  b: number[],
  c: number[],
  sizeX: number,
  sizeY: number,
  visit: (x: number, y: number, u: number, v: number, w: number) => void,
) {
  const d = (b[1]! - c[1]!) * (a[0]! - c[0]!) + (c[0]! - b[0]!) * (a[1]! - c[1]!);
  if (Math.abs(d) < 1e-8) return;
  const x0 = Math.max(0, Math.floor(Math.min(a[0]!, b[0]!, c[0]!))),
    x1 = Math.min(sizeX - 1, Math.ceil(Math.max(a[0]!, b[0]!, c[0]!)));
  const y0 = Math.max(0, Math.floor(Math.min(a[1]!, b[1]!, c[1]!))),
    y1 = Math.min(sizeY - 1, Math.ceil(Math.max(a[1]!, b[1]!, c[1]!)));
  for (let y = y0; y <= y1; y++)
    for (let x = x0; x <= x1; x++) {
      const u = ((b[1]! - c[1]!) * (x + 0.5 - c[0]!) + (c[0]! - b[0]!) * (y + 0.5 - c[1]!)) / d;
      const v = ((c[1]! - a[1]!) * (x + 0.5 - c[0]!) + (a[0]! - c[0]!) * (y + 0.5 - c[1]!)) / d,
        w = 1 - u - v;
      if (u >= -1e-6 && v >= -1e-6 && w >= -1e-6) visit(x, y, u, v, w);
    }
}
export function buildProjection(
  mesh: Mesh,
  pose: Float32Array,
  width: number,
  height: number,
): Projection {
  const scale = Math.min(1, 768 / Math.max(width, height));
  const w = Math.max(1, Math.round(width * scale)),
    h = Math.max(1, Math.round(height * scale));
  const projected = Array.from({ length: mesh.count }, (_, i) => [
    ((pose[i * 6]! + 1) * w) / 2,
    ((1 - pose[i * 6 + 1]!) * h) / 2,
    pose[i * 6 + 2]!,
  ]);
  // Bin triangle planes conservatively, then test depth at the exact projected
  // texel position. Comparing against pixel-center depth with a slope allowance
  // leaks paint onto nearby hidden surfaces, especially at grazing angles.
  const tileSize = 8,
    columns = Math.ceil(w / tileSize),
    rows = Math.ceil(h / tileSize);
  const bins: number[][] = Array.from({ length: columns * rows }, () => []);
  const faces: { a: number[]; b: number[]; c: number[]; inverse: number }[] = [];
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const a = projected[mesh.indices[i]!]!,
      b = projected[mesh.indices[i + 1]!]!,
      c = projected[mesh.indices[i + 2]!]!;
    const determinant = (b[1]! - c[1]!) * (a[0]! - c[0]!) + (c[0]! - b[0]!) * (a[1]! - c[1]!);
    if (Math.abs(determinant) < 1e-8) continue;
    const face = faces.push({ a, b, c, inverse: 1 / determinant }) - 1;
    const left = Math.max(0, Math.floor(Math.min(a[0]!, b[0]!, c[0]!) / tileSize));
    const right = Math.min(columns - 1, Math.floor(Math.max(a[0]!, b[0]!, c[0]!) / tileSize));
    const top = Math.max(0, Math.floor(Math.min(a[1]!, b[1]!, c[1]!) / tileSize));
    const bottom = Math.min(rows - 1, Math.floor(Math.max(a[1]!, b[1]!, c[1]!) / tileSize));
    for (let y = top; y <= bottom; y++)
      for (let x = left; x <= right; x++) bins[y * columns + x]!.push(face);
  }
  function hidden(x: number, y: number, depth: number) {
    const bin = bins[Math.floor(y / tileSize) * columns + Math.floor(x / tileSize)]!;
    for (const index of bin) {
      const { a, b, c, inverse } = faces[index]!;
      const u = ((b[1]! - c[1]!) * (x - c[0]!) + (c[0]! - b[0]!) * (y - c[1]!)) * inverse;
      const v = ((c[1]! - a[1]!) * (x - c[0]!) + (a[0]! - c[0]!) * (y - c[1]!)) * inverse;
      const t = 1 - u - v;
      if (
        u >= -1e-8 &&
        v >= -1e-8 &&
        t >= -1e-8 &&
        u * a[2]! + v * b[2]! + t * c[2]! < depth - 1e-7
      )
        return true;
    }
    return false;
  }
  const result: Projection = {
    x: new Float32Array(65536),
    y: new Float32Array(65536),
    depth: new Float32Array(65536).fill(Infinity),
    visible: new Uint8Array(65536),
    used: new Uint8Array(65536),
  };
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const ids = [mesh.indices[i]!, mesh.indices[i + 1]!, mesh.indices[i + 2]!];
    const [a, b, c] = ids.map((id) => projected[id]!) as [number[], number[], number[]];
    const [ua, ub, uc] = ids.map((id) => [mesh.uv[id * 2]! * 256, mesh.uv[id * 2 + 1]! * 256]) as [
      number[],
      number[],
      number[],
    ];
    const area = (b[1]! - c[1]!) * (a[0]! - c[0]!) + (c[0]! - b[0]!) * (a[1]! - c[1]!);
    triangle(ua, ub, uc, 256, 256, (x, y, u, v, t) => {
      const index = y * 256 + x;
      result.used[index] = 1;
      if (Math.abs(area) < 1e-8) return;
      const sx = u * a[0]! + v * b[0]! + t * c[0]!,
        sy = u * a[1]! + v * b[1]! + t * c[1]!,
        z = u * a[2]! + v * b[2]! + t * c[2]!;
      const px = Math.floor(sx),
        py = Math.floor(sy);
      if (px < 0 || py < 0 || px >= w || py >= h || z >= result.depth[index]! || hidden(sx, sy, z))
        return;
      result.visible[index] = 1;
      result.depth[index] = z;
      result.x[index] = (sx / w) * width;
      result.y[index] = (sy / h) * height;
    });
  }
  return result;
}
/** Rest-space charts keep curated fills independent of inspection camera and pose.
 * Each mesh supplies its own UV compatibility mask. Shared texels choose the
 * source-camera frontmost contributor once, then repeat on matching surfaces.
 * Glob bodies use height bands; flying explorers use their long body axis. */
export function buildFillChart(mesh: Mesh, view: ViewTransform, model: string) {
  const world = Array.from({ length: mesh.count }, (_, i) =>
    point(view.clipToModel, mesh.poses[i * 6]!, mesh.poses[i * 6 + 1]!, mesh.poses[i * 6 + 2]!),
  );
  const low = [0, 1, 2].map((k) => Math.min(...world.map((p) => p[k]!)));
  const high = [0, 1, 2].map((k) => Math.max(...world.map((p) => p[k]!)));
  const axis = model === 'explorer' ? 1 : 2;
  const x = new Float32Array(65536),
    y = new Float32Array(65536);
  const used = new Uint8Array(65536),
    depth = new Float32Array(65536).fill(Infinity);
  for (let t = 0; t < mesh.indices.length; t += 3) {
    const ids = [mesh.indices[t]!, mesh.indices[t + 1]!, mesh.indices[t + 2]!];
    const uv = ids.map((id) => [mesh.uv[id * 2]! * 256, mesh.uv[id * 2 + 1]! * 256]);
    triangle(uv[0]!, uv[1]!, uv[2]!, 256, 256, (u, v, a, b, c) => {
      const at = v * 256 + u,
        weights = [a, b, c];
      const z = ids.reduce((sum, id, j) => sum + weights[j]! * mesh.poses[id * 6 + 2]!, 0);
      if (z >= depth[at]!) return;
      depth[at] = z;
      used[at] = 1;
      const p = [0, 1, 2].map((k) =>
        ids.reduce((sum, id, j) => sum + weights[j]! * world[id]![k]!, 0),
      );
      // Bands follow the body; the second coordinate mirrors spots across it.
      x[at] = ((p[axis]! - low[axis]!) / Math.max(0.001, high[axis]! - low[axis]!)) * 256;
      y[at] = Math.abs((p[0]! - low[0]!) / Math.max(0.001, high[0]! - low[0]!) - 0.5) * 512;
    });
  }
  return { x, y, used };
}
export function strokeCoverage(
  projection: Projection,
  from: [number, number],
  to: [number, number],
  radius: number,
  hardness: number,
) {
  const coverage = new Float32Array(65536),
    dx = to[0] - from[0],
    dy = to[1] - from[1],
    length = dx * dx + dy * dy;
  for (let i = 0; i < coverage.length; i++) {
    if (!projection.visible[i]) continue;
    const t = length
      ? Math.max(
          0,
          Math.min(
            1,
            ((projection.x[i]! - from[0]) * dx + (projection.y[i]! - from[1]) * dy) / length,
          ),
        )
      : 0;
    const distance =
      Math.hypot(projection.x[i]! - from[0] - t * dx, projection.y[i]! - from[1] - t * dy) / radius;
    if (distance <= 1)
      coverage[i] = distance <= hardness ? 1 : (1 - distance) / Math.max(0.001, 1 - hardness);
  }
  return coverage;
}
/** One-texel seam padding, restricted to unused pixels in this model quadrant. */
export function padCoverage(coverage: Float32Array, used: Uint8Array) {
  const padded = coverage.slice();
  for (let y = 0; y < 256; y++)
    for (let x = 0; x < 256; x++) {
      const i = y * 256 + x;
      if (used[i]) continue;
      for (const [dx, dy] of [
        [-1, 0],
        [1, 0],
        [0, -1],
        [0, 1],
      ]) {
        const nx = x + dx!,
          ny = y + dy!;
        if (nx >= 0 && nx < 256 && ny >= 0 && ny < 256)
          padded[i] = Math.max(padded[i]!, coverage[ny * 256 + nx]!);
      }
    }
  return padded;
}
export type PatternKind =
  'solid' | 'stripes' | 'spots' | 'checker' | 'chevrons' | 'waves' | 'speckles';
export type PatternOptions = {
  kind: PatternKind;
  scale: number;
  rotation: number;
  offsetX: number;
  offsetY: number;
  density: number;
  seed: number;
  whole: boolean;
};
export const DEFAULT_PATTERN: PatternOptions = {
  kind: 'stripes',
  scale: 48,
  rotation: 0,
  offsetX: 0,
  offsetY: 0,
  density: 0.5,
  seed: 1,
  whole: false,
};
function hash(x: number, y: number, seed: number) {
  let v = Math.imul(x ^ seed, 374761393) + Math.imul(y, 668265263);
  v = Math.imul(v ^ (v >>> 13), 1274126177);
  return ((v ^ (v >>> 16)) >>> 0) / 4294967296;
}
export function patternValue(x: number, y: number, options: PatternOptions) {
  const a = (options.rotation * Math.PI) / 180,
    px = x + options.offsetX,
    py = y + options.offsetY;
  const u = (Math.cos(a) * px - Math.sin(a) * py) / options.scale,
    v = (Math.sin(a) * px + Math.cos(a) * py) / options.scale;
  const fract = (n: number) => n - Math.floor(n),
    density = options.density;
  switch (options.kind) {
    case 'solid':
      return 1;
    case 'stripes':
      return fract(u) < density ? 1 : 0;
    case 'spots':
      return Math.hypot(fract(u) - 0.5, fract(v) - 0.5) < density * 0.65 ? 1 : 0;
    case 'checker':
      return (Math.floor(u) + Math.floor(v)) % 2 === 0 ? 1 : 0;
    case 'chevrons':
      return fract(v + Math.abs(fract(u) * 2 - 1)) < density ? 1 : 0;
    case 'waves':
      return fract(u + Math.sin(v * Math.PI * 2) * 0.3) < density ? 1 : 0;
    case 'speckles': {
      if (options.whole) {
        const noise = (a: number, b: number) => {
          const ix = Math.floor(a),
            iy = Math.floor(b);
          const sx = fract(a) ** 2 * (3 - 2 * fract(a)),
            sy = fract(b) ** 2 * (3 - 2 * fract(b));
          const top = hash(ix, iy, options.seed) * (1 - sx) + hash(ix + 1, iy, options.seed) * sx;
          const bottom =
            hash(ix, iy + 1, options.seed) * (1 - sx) + hash(ix + 1, iy + 1, options.seed) * sx;
          return top * (1 - sy) + bottom * sy;
        };
        return noise(u, v) * 0.7 + noise(u * 2.3, v * 2.3) * 0.3 < density ? 1 : 0;
      }
      return hash(Math.floor(u), Math.floor(v), options.seed) < density ? 1 : 0;
    }
  }
}
