// SPDX-License-Identifier: GPL-3.0-or-later
/* GSB1 blend-shape contract shared with libgag/src/SkinShapeModelTest.cpp. */
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { decodeShapes, evaluateShapes } from '../src/skins/shapes.ts';
import { projectPose, shapeGeometry, unitClipFormat } from '../src/skins/geometry.ts';
import { buildFillChart } from '../src/skins/projection.ts';

const fixture = JSON.parse(
  readFileSync(new URL('../../../../test/fixtures/skins/shapes.json', import.meta.url), 'utf8'),
) as {
  hex: string;
  header: Record<string, number>;
  expected: Record<string, number[][]>;
  malformed: Record<string, string>;
};
const toBuffer = (hex: string) => Uint8Array.from(Buffer.from(hex, 'hex')).buffer;
const MALFORMED_MESSAGES: Record<string, string> = {
  badMagic: 'Unsupported shape format',
  truncated: 'Invalid shape dimensions',
  trailing: 'Invalid shape dimensions',
  badIndex: 'Invalid shape index',
  zeroScale: 'Invalid shape scale',
};
/** Largest absolute difference between a pose and its expected values. */
function deviation(pose: Float32Array, expected: number[][]) {
  let worst = 0;
  expected.forEach((vertex, v) =>
    vertex.forEach((value, k) => {
      worst = Math.max(worst, Math.abs(pose[v * 6 + k]! - value));
    }),
  );
  return worst;
}
function worstNormalLength(pose: Float32Array, count: number) {
  let worst = 0;
  for (let v = 0; v < count; v++)
    worst = Math.max(
      worst,
      Math.abs(Math.hypot(pose[v * 6 + 3]!, pose[v * 6 + 4]!, pose[v * 6 + 5]!) - 1),
    );
  return worst;
}

describe('GSB1 shared conformance', () => {
  it('decodes the fixture header and evaluates the analytic frames', () => {
    const model = decodeShapes(toBuffer(fixture.hex));
    expect(model.count).toBe(fixture.header.vertices);
    expect(model.shapes).toBe(fixture.header.shapes);
    expect(model.normalShapes).toBe(fixture.header.normalShapes);
    expect(model.clips.length).toBe(fixture.header.clips);
    expect(model.logicalSize).toBe(fixture.header.logicalSize);
    for (const [key, expected] of Object.entries(fixture.expected)) {
      const [clip, frame] = key.split(':').map(Number) as [number, number];
      expect(deviation(evaluateShapes(model, clip, frame), expected), key).toBeLessThan(1e-5);
    }
    for (let frame = 0; frame < 256; frame++) {
      const pose = evaluateShapes(model, 1, frame);
      expect(pose.every(Number.isFinite)).toBe(true);
      expect(worstNormalLength(pose, model.count)).toBeLessThan(1e-5);
    }
    const output = new Float32Array(model.count * 6);
    expect(evaluateShapes(model, 0, 3, true, output)).toBe(output);
    const modelSpace = evaluateShapes(model, 0, 3, false);
    expect(Array.from(modelSpace)).not.toEqual(Array.from(output));
    expect(() => evaluateShapes(model, 0, 256)).toThrow('Invalid shape frame');
    expect(() => evaluateShapes(model, 2, 0)).toThrow('Invalid shape frame');
  });
  for (const [name, hex] of Object.entries(fixture.malformed))
    it(`rejects ${name}`, () => {
      expect(() => decodeShapes(toBuffer(hex))).toThrow(MALFORMED_MESSAGES[name]);
    });
  it('drives Studio projection through the mesh adapter with a fixed chart', () => {
    const model = decodeShapes(toBuffer(fixture.hex));
    const { mesh, view } = shapeGeometry(model, 1, '0'.repeat(64));
    expect(mesh.frames).toBe(256);
    expect(mesh.rest.length).toBe(model.count * 6);
    expect(worstNormalLength(mesh.rest, model.count)).toBeLessThan(1e-5);
    expect(view.meshSha256).toBe('0'.repeat(64));
    const chart = buildFillChart(mesh, view, 'worker');
    for (const frame of [0, 7, 255]) {
      const projected = projectPose(
        mesh,
        view,
        frame,
        { yaw: 0, pitch: 0, zoom: 1, game: true, angle: 0 },
        1,
      );
      const direct = evaluateShapes(model, 1, frame);
      let worst = 0;
      for (let v = 0; v < model.count; v++)
        for (let k = 0; k < 6; k++)
          worst = Math.max(
            worst,
            Math.abs(projected[v * 6 + k]! - direct[v * 6 + k]! / (k < 2 ? 1.25 : 1)),
          );
      expect(worst, `frame ${frame}`).toBeLessThan(1e-5);
    }
    // Animation never moves the chart that fill and pattern tools paint into.
    expect(buildFillChart(mesh, view, 'worker')).toEqual(chart);
    // The inspection camera frames the rest pose and stays finite.
    const inspected = projectPose(
      mesh,
      view,
      3,
      { yaw: 1, pitch: 0, zoom: 1, game: false, angle: 0 },
      1.5,
    );
    expect(inspected.every(Number.isFinite)).toBe(true);
  });
  it('animates workers and warriors from blend shapes, the explorer from its rig', () => {
    expect(unitClipFormat('worker-walk')).toBe('gsb');
    expect(unitClipFormat('warrior-swim')).toBe('gsb');
    expect(unitClipFormat('explorer-fly')).toBe('gsr');
    expect(unitClipFormat('swarm')).toBeUndefined();
  });
});
it.each([
  ['worker-walk', 2898],
  ['worker-swim', 2898],
  ['worker-harvest', 2898],
  ['warrior-walk', 2514],
  ['warrior-swim', 2514],
  ['warrior-fight', 2514],
] as const)('installed %s decodes and stays close to its baked clip', (asset, count) => {
  const buffer = readFileSync(new URL(`../public/skins/models/${asset}.gsb`, import.meta.url));
  const model = decodeShapes(Uint8Array.from(buffer).buffer);
  expect(model.count).toBe(count);
  expect(model.clips.length).toBe(1);
  // The baked clip the shapes were fitted to: same layout, clip-space poses.
  const baked = readFileSync(new URL(`../public/skins/models/${asset}.gsk`, import.meta.url));
  const bakedBytes = Uint8Array.from(baked).buffer;
  const bakedCount = new DataView(bakedBytes).getUint32(4, true);
  const indexCount = new DataView(bakedBytes).getUint32(8, true);
  expect(bakedCount).toBe(count);
  const poses = new Float32Array(bakedBytes, 20 + count * 8 + indexCount * 4);
  let worstPosition = 0,
    worstNormal = 0,
    squared = 0,
    measured = 0;
  for (let frame = 0; frame < 256; frame += 5) {
    const pose = evaluateShapes(model, 0, frame);
    expect(pose.every(Number.isFinite)).toBe(true);
    worstNormal = Math.max(worstNormal, worstNormalLength(pose, count));
    const logical = model.logicalSize;
    for (let v = 0; v < count; v++) {
      const at = frame * count * 6 + v * 6;
      // Clip-space error in logical pixels: clip units span logical / 2 pixels.
      const dx = ((pose[v * 6]! - poses[at]!) * logical) / 2;
      const dy = ((pose[v * 6 + 1]! - poses[at + 1]!) * logical) / 2;
      const distance = Math.hypot(dx, dy);
      worstPosition = Math.max(worstPosition, distance);
      squared += distance * distance;
      measured++;
    }
  }
  expect(worstNormal).toBeLessThan(1e-5);
  // Blend shapes reproduce the baked frames to a fraction of a logical pixel
  // on average; a few vertices in the fastest limb motion stray further.
  expect(Math.sqrt(squared / measured)).toBeLessThan(0.5);
  expect(worstPosition).toBeLessThan(4);
});
