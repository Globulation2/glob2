/* GSB1 blend-shape contract shared with libgag/src/SkinShapeModelTest.cpp. */
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { decodeShapes, evaluateShapes } from '../src/skins/shapes.ts';
import { candidateFormat, projectPose, shapeGeometry } from '../src/skins/geometry.ts';

const fixture = JSON.parse(
  readFileSync(new URL('../../../../test/fixtures/skins/shapes.json', import.meta.url), 'utf8'),
) as {
  hex: string;
  header: Record<string, number>;
  expected: Record<string, number[][]>;
  malformed: Record<string, string>;
};
const toBuffer = (hex: string) => Uint8Array.from(Buffer.from(hex, 'hex')).buffer;

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
      const pose = evaluateShapes(model, clip, frame);
      expected.forEach((vertex, v) =>
        vertex.forEach((value, k) => expect(pose[v * 6 + k]).toBeCloseTo(value, 5)),
      );
    }
    for (let frame = 0; frame < 256; frame++) {
      const pose = evaluateShapes(model, 1, frame);
      expect(pose.every(Number.isFinite)).toBe(true);
      for (let v = 0; v < model.count; v++)
        expect(Math.hypot(pose[v * 6 + 3]!, pose[v * 6 + 4]!, pose[v * 6 + 5]!)).toBeCloseTo(1, 5);
    }
    const output = new Float32Array(model.count * 6);
    expect(evaluateShapes(model, 0, 3, true, output)).toBe(output);
    expect(() => evaluateShapes(model, 0, 256)).toThrow('Invalid shape frame');
    expect(() => evaluateShapes(model, 2, 0)).toThrow('Invalid shape frame');
  });
  it('rejects malformed assets', () => {
    for (const hex of Object.values(fixture.malformed))
      expect(() => decodeShapes(toBuffer(hex))).toThrow();
  });
  it('drives Studio projection through the mesh adapter', () => {
    const model = decodeShapes(toBuffer(fixture.hex));
    const { mesh, view } = shapeGeometry(model, 1, 'abc');
    expect(mesh.shapes).toBe(model);
    expect(mesh.clip).toBe(1);
    expect(mesh.frames).toBe(256);
    expect(mesh.poses.length).toBe(model.count * 6);
    expect(view.meshSha256).toBe('abc');
    const projected = projectPose(mesh, view, 7, { yaw: 0, zoom: 1, game: true, angle: 0 }, 1);
    const direct = evaluateShapes(model, 1, 7);
    for (let v = 0; v < model.count; v++)
      for (let k = 0; k < 6; k++)
        expect(projected[v * 6 + k]).toBeCloseTo(direct[v * 6 + k]! / (k < 2 ? 1.25 : 1), 5);
  });
  it('routes workers and warriors to shapes and the explorer to its rig', () => {
    expect(candidateFormat('worker-walk')).toBe('shapes');
    expect(candidateFormat('warrior-swim')).toBe('shapes');
    expect(candidateFormat('explorer-fly')).toBe('rig');
  });
});
it.each([
  ['worker-walk', 2834],
  ['worker-swim', 2834],
  ['worker-harvest', 2834],
  ['warrior-walk', 2450],
  ['warrior-swim', 2450],
  ['warrior-fight', 2450],
] as const)('decodes installed %s and produces finite unit normals', (asset, count) => {
  const buffer = readFileSync(new URL(`../public/skins/models/${asset}.gsb`, import.meta.url));
  const model = decodeShapes(Uint8Array.from(buffer).buffer);
  expect(model.count).toBe(count);
  for (let frame = 0; frame < 256; frame += 5) {
    const pose = evaluateShapes(model, 0, frame);
    expect(pose.every(Number.isFinite)).toBe(true);
    for (let v = 0; v < model.count; v++)
      expect(Math.hypot(pose[v * 6 + 3]!, pose[v * 6 + 4]!, pose[v * 6 + 5]!)).toBeCloseTo(1, 5);
  }
});
