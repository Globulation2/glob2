import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  decode,
  projectPose,
  DEFAULT_CAMERA,
  validateView,
  type ViewTransform,
  type Mesh,
} from '../src/skins/geometry.ts';
import {
  buildProjection,
  buildFillChart,
  strokeCoverage,
  padCoverage,
  DEFAULT_PATTERN,
  patternValue,
} from '../src/skins/projection.ts';
const root = resolve(import.meta.dirname, '../../../..');
function asset(name: string) {
  const b = readFileSync(resolve(root, `data/skins/colony-v1/${name}.gsk`));
  return {
    mesh: decode(b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength)),
    view: JSON.parse(
      readFileSync(resolve(root, `data/skins/colony-v1/${name}.view.json`), 'utf8'),
    ) as ViewTransform,
  };
}
describe('skin projection', () => {
  it('excludes hidden geometry and paints a continuous screen-space stroke', () => {
    const mesh: Mesh = {
      count: 6,
      frames: 1,
      uv: new Float32Array([0, 0, 0.5, 0, 0, 1, 0.5, 0, 1, 0, 0.5, 1]),
      indices: new Uint32Array([0, 1, 2, 3, 4, 5]),
      poses: new Float32Array(36),
    };
    const pose = new Float32Array([
      -0.8, -0.8, 0, 0, 0, 1, 0.8, -0.8, 0, 0, 0, 1, -0.8, 0.8, 0, 0, 0, 1, -0.8, -0.8, 0.5, 0, 0,
      1, 0.8, -0.8, 0.5, 0, 0, 1, -0.8, 0.8, 0.5, 0, 0, 1,
    ]);
    const p = buildProjection(mesh, pose, 400, 400);
    let visible = 0,
      hidden = 0;
    for (let i = 0; i < 65536; i++) {
      if (i % 256 < 128) visible += p.visible[i] ?? 0;
      else hidden += p.visible[i] ?? 0;
    }
    expect(visible).toBeGreaterThan(10000);
    expect(hidden).toBe(0);
    const coverage = strokeCoverage(p, [45, 300], [280, 300], 10, 1);
    expect(coverage.some((v) => v > 0)).toBe(true);
    for (let i = 0; i < 65536; i++)
      if (coverage[i]) expect(Math.abs((p.y[i] ?? 0) - 300)).toBeLessThanOrEqual(10.01);
  });
  it('does not paint through a nearby sloping occluder', () => {
    const pose = new Float32Array([
      -0.8, -0.8, -0.4, 0, 0, 1, 0.8, -0.8, 0.4, 0, 0, 1, -0.8, 0.8, -0.4, 0, 0, 1, -0.8, -0.8,
      -0.395, 0, 0, 1, 0.8, -0.8, 0.405, 0, 0, 1, -0.8, 0.8, -0.395, 0, 0, 1,
    ]);
    const mesh: Mesh = {
      count: 6,
      frames: 1,
      uv: new Float32Array([0, 0, 0.5, 0, 0, 1, 0.5, 0, 1, 0, 0.5, 1]),
      indices: new Uint32Array([0, 1, 2, 3, 4, 5]),
      poses: pose,
    };
    // Rear geometry has entirely separate UVs and is wholly occluded. Its depth
    // separation is smaller than one screen pixel's depth slope.
    for (const [width, height] of [
      [400, 400],
      [1600, 900],
    ]) {
      const projection = buildProjection(mesh, pose, width!, height!);
      let front = 0,
        rear = 0;
      for (let i = 0; i < projection.visible.length; i++) {
        if (i % 256 < 128) front += projection.visible[i] ?? 0;
        else rear += projection.visible[i] ?? 0;
      }
      expect(front).toBe(16384);
      expect(rear).toBe(0);
    }
  });
  it('does not paint edge-on geometry absent from the rendered surface', () => {
    const pose = new Float32Array([-0.8, 0, 0, 0, 0, 1, 0.8, 0, 0, 0, 0, 1, 0, 0, 0.5, 0, 0, 1]);
    const mesh: Mesh = {
      count: 3,
      frames: 1,
      poses: pose,
      uv: new Float32Array([0, 0, 1, 0, 0, 1]),
      indices: new Uint32Array([0, 1, 2]),
    };
    const projection = buildProjection(mesh, pose, 400, 400);
    expect(projection.used.some(Boolean)).toBe(true);
    expect(projection.visible.some(Boolean)).toBe(false);
  });
  it('pads only unused texels and never wraps a model edge', () => {
    const mask = new Float32Array(65536),
      used = new Uint8Array(65536);
    mask[255] = 1;
    used[255] = 1;
    used[511] = 1;
    const padded = padCoverage(mask, used);
    expect(padded[254]).toBe(1);
    expect(padded[256]).toBe(0);
    expect(padded[511]).toBe(0);
  });
  it('retains zero-angle framing and makes true 3D rotations', () => {
    for (const name of [
      'worker-walk',
      'warrior-fight',
      'explorer-fly',
      'swarm',
      'swarm-crown',
      'swarm-skep',
    ]) {
      const { mesh, view } = asset(name);
      validateView(view);
      const game = projectPose(mesh, view, 0, { ...DEFAULT_CAMERA, game: true }, 1);
      for (let i = 0; i < mesh.count; i++) {
        expect(game[i * 6]).toBeCloseTo((mesh.poses[i * 6] ?? 0) / 1.25, 4);
        expect(game[i * 6 + 1]).toBeCloseTo((mesh.poses[i * 6 + 1] ?? 0) / 1.25, 4);
      }
      const back = projectPose(mesh, view, 0, { ...DEFAULT_CAMERA, yaw: Math.PI }, 1);
      expect(back.every(Number.isFinite)).toBe(true);
      expect(back).not.toEqual(game);
    }
  });
  it('keeps every swarm angle inside the standardized frame', () => {
    for (const shape of [
      'swarm',
      'swarm-crown',
      'swarm-clutch',
      'swarm-toadstool',
      'swarm-coral',
      'swarm-skep',
      'swarm-bloom',
    ]) {
      const { mesh, view } = asset(shape);
      for (let angle = 0; angle < 360; angle += 5) {
        const pose = projectPose(mesh, view, 0, { ...DEFAULT_CAMERA, game: true, angle }, 1);
        for (let i = 0; i < mesh.count; i++) {
          expect(Math.abs(pose[i * 6] ?? 0), `${shape} angle ${angle}`).toBeLessThan(1);
          expect(Math.abs(pose[i * 6 + 1] ?? 0), `${shape} angle ${angle}`).toBeLessThan(1);
        }
      }
    }
  });
  it('curated fill masks follow every model topology and stay fixed across views', () => {
    for (const name of [
      'worker-walk',
      'worker-swim',
      'worker-harvest',
      'warrior-walk',
      'warrior-swim',
      'warrior-fight',
      'explorer-fly',
      'swarm',
      'swarm-crown',
      'swarm-clutch',
      'swarm-toadstool',
      'swarm-coral',
      'swarm-skep',
      'swarm-bloom',
    ]) {
      const { mesh, view } = asset(name);
      const chart = buildFillChart(mesh, view, name.split('-')[0] ?? 'swarm');
      const projection = buildProjection(
        mesh,
        projectPose(mesh, view, 0, DEFAULT_CAMERA, 1),
        384,
        384,
      );
      expect(chart.used, name).toEqual(projection.used);
      for (const size of [32, 64, 96]) {
        for (const kind of ['stripes', 'spots', 'speckles'] as const) {
          let painted = 0,
            total = 0;
          for (let i = 0; i < 65536; i++)
            if (chart.used[i]) {
              total++;
              painted += patternValue(chart.x[i] ?? 0, chart.y[i] ?? 0, {
                ...DEFAULT_PATTERN,
                kind,
                scale: size,
                whole: true,
              });
            }
          expect(painted, `${name} ${kind} ${size}`).toBeGreaterThan(total * 0.03);
          expect(painted).toBeLessThan(total * 0.98);
        }
      }
    }
  });
  it('patterns are deterministic and respond to projection placement', () => {
    const a = Array.from({ length: 200 }, (_, i) =>
      patternValue(i, 33, { ...DEFAULT_PATTERN, kind: 'speckles' }),
    );
    expect(a).toEqual(
      Array.from({ length: 200 }, (_, i) =>
        patternValue(i, 33, { ...DEFAULT_PATTERN, kind: 'speckles' }),
      ),
    );
    expect(patternValue(0, 0, DEFAULT_PATTERN)).not.toBe(patternValue(30, 0, DEFAULT_PATTERN));
    expect(patternValue(0, 0, { ...DEFAULT_PATTERN, offsetX: 30 })).toBe(
      patternValue(30, 0, DEFAULT_PATTERN),
    );
  });
});
