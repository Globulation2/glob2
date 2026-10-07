// SPDX-License-Identifier: GPL-3.0-or-later
/* GSR1 bone-rig contract shared with libgag/src/SkinModelTest.cpp. */
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { rigGeometry, projectPose } from '../src/skins/geometry.ts';
import { buildFillChart } from '../src/skins/projection.ts';
import { decodeRig, evaluateRig, paletteAtTime } from '../src/skins/rig.ts';
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
/** Largest absolute difference between a pose and its expected values. */
function deviation(actual: ArrayLike<number>, expected: ArrayLike<number>) {
  let worst = 0;
  for (let i = 0; i < expected.length; i++)
    worst = Math.max(worst, Math.abs(actual[i]! - expected[i]!));
  return worst;
}
describe('GSR1 shared conformance', () => {
  it('matches analytic poses for every frame with antipodal quaternion keys', () => {
    const model = decodeRig(bytes()),
      output = new Float32Array(model.count * 6);
    for (let clip = 0; clip < 2; clip++)
      for (let frame = 0; frame < 256; frame++) {
        expect(evaluateRig(model, clip, frame, true, output)).toBe(output);
        expect(
          deviation(output, fixture.expected[frame]!),
          `clip ${clip} frame ${frame}`,
        ).toBeLessThan(1e-5);
      }
    expect(deviation(evaluateRig(model, 0, 0, false), model.rest)).toBeLessThan(1e-6);
    const beforeInvalidRequest = output.slice();
    expect(() => evaluateRig(model, 0, 256, true, output)).toThrow('Invalid rig sample');
    expect(output).toEqual(beforeInvalidRequest);
    expect(() => evaluateRig(model, -1, 0)).toThrow('Invalid rig clip');
    expect(() => evaluateRig(model, 0, 0.5)).toThrow('Invalid rig sample');
  });
  it('applies affine camera translation independently of rounded influence sums', () => {
    const model = decodeRig(Uint8Array.from(Buffer.from(fixture.affineCamera.hex, 'hex')).buffer);
    const roundedSum = model.influences[0]!.weights.reduce(
      (sum, weight) => Math.fround(sum + weight),
      0,
    );
    expect(roundedSum).not.toBe(1);
    expect(deviation(evaluateRig(model, 0, 0), fixture.affineCamera.expected)).toBeLessThan(1e-5);
  });
  it('wraps time on both sides of the full cycle', () => {
    const model = decodeRig(bytes());
    for (const time of [0, 0.125, 0.5, 1, 1.5, 1.999]) {
      const a = paletteAtTime(model, 0, time, 0);
      for (const other of [time + 2, time - 2]) {
        const b = paletteAtTime(model, 0, other, 0);
        expect(deviation(a.positions.flat(), b.positions.flat())).toBeLessThan(1e-5);
      }
    }
    expect(() => paletteAtTime(model, 0, Infinity, 0)).toThrow('Invalid rig pose');
    expect(() => paletteAtTime(model, 0, 0, NaN)).toThrow('Invalid rig pose');
  });
  it('owns immutable copies independent of the input buffer', () => {
    const buffer = bytes(),
      model = decodeRig(buffer),
      rest = Array.from(model.rest);
    new Uint8Array(buffer).fill(0);
    expect(Array.from(model.rest)).toEqual(rest);
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
      else expect(() => decodeRig(buffer)).toThrow(/Invalid rig|Unbounded rig/);
    });
  for (const bad of fixture.malformed)
    it(`rejects ${bad.name}`, () => {
      const buffer = bytes();
      new DataView(buffer).setUint32(bad.offset, bad.word, true);
      expect(() => decodeRig(buffer)).toThrow(/rig/);
    });
  it('rejects truncation and trailing data', () => {
    const buffer = bytes();
    for (const length of [0, 27, 28, 220, buffer.byteLength - 1])
      expect(() => decodeRig(buffer.slice(0, length))).toThrow(/rig/);
    const extra = new Uint8Array(buffer.byteLength + 1);
    extra.set(new Uint8Array(buffer));
    expect(() => decodeRig(extra.buffer)).toThrow('Invalid rig dimensions');
  });
});

it('uses the same displayed rig pose for Studio projection and a fixed rest chart', () => {
  const model = decodeRig(bytes());
  const { mesh, view } = rigGeometry(model, 0, '0'.repeat(64));
  const chart = buildFillChart(mesh, view, 'worker');
  for (const frame of [0, 31, 32, 63, 64, 255]) {
    const cpu = evaluateRig(model, 0, frame);
    const projected = projectPose(
      mesh,
      view,
      frame,
      { yaw: 0, pitch: 0, zoom: 1, game: true, angle: 0 },
      1,
    );
    let worst = 0;
    for (let v = 0; v < model.count; v++)
      for (let k = 0; k < 6; k++)
        worst = Math.max(
          worst,
          Math.abs(projected[v * 6 + k]! - cpu[v * 6 + k]! / (k < 2 ? 1.25 : 1)),
        );
    expect(worst, `frame ${frame}`).toBeLessThan(1e-5);
  }
  expect(buildFillChart(mesh, view, 'worker')).toEqual(chart);
  expect(mesh.rest.length).toBe(model.count * 6);
  const inspected = projectPose(
    mesh,
    view,
    40,
    { yaw: 0.5, pitch: 0, zoom: 1, game: false, angle: 0 },
    1,
  );
  expect(inspected.every(Number.isFinite)).toBe(true);
});
it('installed explorer-fly decodes and produces finite unit normals', () => {
  const buffer = readFileSync(new URL('../public/skins/models/explorer-fly.gsr', import.meta.url));
  const model = decodeRig(Uint8Array.from(buffer).buffer);
  expect(model.count).toBe(1330);
  expect(model.bones.length).toBe(4);
  let worst = 0;
  for (let frame = 0; frame < 256; frame++) {
    const pose = evaluateRig(model, 0, frame);
    expect(pose.every(Number.isFinite)).toBe(true);
    for (let v = 0; v < model.count; v++)
      worst = Math.max(
        worst,
        Math.abs(Math.hypot(pose[v * 6 + 3]!, pose[v * 6 + 4]!, pose[v * 6 + 5]!) - 1),
      );
  }
  expect(worst).toBeLessThan(1e-5);
});
