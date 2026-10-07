// SPDX-License-Identifier: GPL-3.0-or-later
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { rigGeometry, projectPose } from '../src/skins/geometry.ts';
import { buildFillChart } from '../src/skins/projection.ts';
import { decodeRig, evaluateRig, rigPalette } from '../src/skins/rig.ts';
const fixture = JSON.parse(
  readFileSync(new URL('../../../../test/fixtures/skins/rig.json', import.meta.url), 'utf8'),
) as {
  hex: string;
  expected: number[][];
  affineCamera: { hex: string; expected: number[] };
  malformed: { name: string; offset: number; word: number }[];
  boundaries: { name: string; accepted: boolean; patches: { offset: number; word: number }[] }[];
};
const bytes = () => Uint8Array.from(Buffer.from(fixture.hex, 'hex')).buffer;
describe('GSR1 shared conformance', () => {
  it('matches analytic poses for every frame with antipodal quaternion keys', () => {
    const model = decodeRig(bytes()),
      output = new Float32Array(model.count * 6);
    for (let clip = 0; clip < 2; clip++)
      for (let sample = 0; sample < 256; sample++) {
        expect(evaluateRig({ model, clip, sample }, true, output)).toBe(output);
        output.forEach((v, i) => expect(v).toBeCloseTo(fixture.expected[sample]![i]!, 5));
      }
    expect(Array.from(evaluateRig({ model, clip: 0, sample: 0 }, false))).toEqual(model.rest);
    const beforeInvalidRequest = output.slice();
    expect(() => evaluateRig({ model, clip: 0, sample: 256 }, true, output)).toThrow();
    expect(output).toEqual(beforeInvalidRequest);
    expect(() => evaluateRig({ model, clip: -1, sample: 0 })).toThrow();
    expect(() => evaluateRig({ model, clip: 0, sample: 0.5 })).toThrow();
  });
  it('applies affine camera translation independently of rounded influence sums', () => {
    const model = decodeRig(Uint8Array.from(Buffer.from(fixture.affineCamera.hex, 'hex')).buffer);
    const roundedSum = model.influences[0]!.weights.reduce(
      (sum, weight) => Math.fround(sum + weight),
      0,
    );
    expect(roundedSum).not.toBe(1);
    const output = evaluateRig({ model, clip: 0, sample: 0 });
    output.forEach((value, i) => expect(value).toBeCloseTo(fixture.affineCamera.expected[i]!, 5));
  });
  it('wraps time on both sides of the full cycle', () => {
    const model = decodeRig(bytes());
    for (const time of [0, 0.125, 0.5, 1, 1.5, 1.999]) {
      const a = rigPalette(model, 0, time, 0);
      for (const other of [time + 2, time - 2]) {
        const b = rigPalette(model, 0, other, 0);
        a.positions.flat().forEach((v, i) => expect(v).toBeCloseTo(b.positions.flat()[i]!, 5));
      }
    }
    expect(() => rigPalette(model, 0, Infinity, 0)).toThrow();
    expect(() => rigPalette(model, 0, 0, NaN)).toThrow();
  });
  it('owns immutable copies independent of the input buffer', () => {
    const buffer = bytes(),
      model = decodeRig(buffer),
      rest = [...model.rest];
    new Uint8Array(buffer).fill(0);
    expect(model.rest).toEqual(rest);
    expect(Object.isFrozen(model)).toBe(true);
    expect(Object.isFrozen(model.clips[0]?.tracks[0]?.rotation)).toBe(true);
    expect(Object.isFrozen(model.influences[0]?.weights)).toBe(true);
  });
  for (const boundary of fixture.boundaries)
    it(`shares native acceptance at ${boundary.name}`, () => {
      const buffer = bytes(),
        view = new DataView(buffer);
      for (const patch of boundary.patches) view.setUint32(patch.offset, patch.word, true);
      if (boundary.accepted) expect(() => decodeRig(buffer)).not.toThrow();
      else expect(() => decodeRig(buffer)).toThrow();
    });
  for (const bad of fixture.malformed)
    it(`rejects ${bad.name}`, () => {
      const buffer = bytes();
      new DataView(buffer).setUint32(bad.offset, bad.word, true);
      expect(() => decodeRig(buffer)).toThrow();
    });
  it('rejects truncation and trailing data', () => {
    const buffer = bytes();
    for (const length of [0, 27, 28, 220, buffer.byteLength - 1])
      expect(() => decodeRig(buffer.slice(0, length))).toThrow();
    const extra = new Uint8Array(buffer.byteLength + 1);
    extra.set(new Uint8Array(buffer));
    expect(() => decodeRig(extra.buffer)).toThrow();
  });
});

it('uses the same displayed rig pose for Studio projection and a fixed rest chart', () => {
  const model = decodeRig(bytes());
  const { mesh, view } = rigGeometry(model, 0, '0'.repeat(64));
  const chart = buildFillChart(mesh, view, 'worker');
  for (const frame of [0, 31, 32, 63, 64, 255]) {
    const cpu = evaluateRig({ model, clip: 0, sample: frame });
    const projected = projectPose(mesh, view, frame, { yaw: 0, zoom: 1, game: true, angle: 0 }, 1);
    for (let v = 0; v < model.count; v++)
      for (let k = 0; k < 6; k++)
        expect(projected[v * 6 + k]).toBeCloseTo(cpu[v * 6 + k]! / (k < 2 ? 1.25 : 1), 5);
  }
  expect(buildFillChart(mesh, view, 'worker')).toEqual(chart);
  expect(mesh.poses.length).toBe(model.count * 6);
});
it.each([
  ['worker-walk', 2834],
  ['worker-swim', 2834],
  ['worker-harvest', 2834],
  ['warrior-walk', 2450],
  ['warrior-swim', 2450],
  ['warrior-fight', 2450],
  ['explorer-fly', 1330],
] as const)('decodes installed %s and produces finite unit normals', (asset, count) => {
  const buffer = readFileSync(new URL(`../public/skins/models/${asset}.gsr`, import.meta.url));
  const model = decodeRig(Uint8Array.from(buffer).buffer);
  expect(model.count).toBe(count);
  for (let sample = 0; sample < 256; sample++) {
    const pose = evaluateRig({ model, clip: 0, sample });
    expect(pose.every(Number.isFinite)).toBe(true);
    for (let v = 0; v < model.count; v++) {
      expect(Math.hypot(pose[v * 6 + 3]!, pose[v * 6 + 4]!, pose[v * 6 + 5]!)).toBeCloseTo(1, 5);
    }
  }
});
