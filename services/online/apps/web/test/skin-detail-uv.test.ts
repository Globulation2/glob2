import { describe, expect, it } from 'vitest';
import { decodeDetailUV } from '../src/skins/geometry.ts';

function fixture() {
  const bytes = new ArrayBuffer(32);
  const view = new DataView(bytes);
  new Uint8Array(bytes, 0, 4).set([71, 85, 86, 49]);
  view.setUint32(4, 3, true);
  [0, 0, 1, 0, 0.5, 1].forEach((value, i) => view.setFloat32(8 + i * 4, value, true));
  return bytes;
}

describe('procedural material unwrap', () => {
  it('loads a separate coordinate stream without changing the paint chart', () => {
    expect(decodeDetailUV(fixture(), 3)).toEqual(new Float32Array([0, 0, 1, 0, 0.5, 1]));
  });
  it('rejects stale dimensions, bad magic and incomplete streams', () => {
    expect(() => decodeDetailUV(fixture(), 4)).toThrow();
    expect(() => decodeDetailUV(fixture().slice(0, 31), 3)).toThrow();
    const bytes = fixture();
    new Uint8Array(bytes)[0] = 0;
    expect(() => decodeDetailUV(bytes, 3)).toThrow();
    const count = fixture();
    new DataView(count).setUint32(4, 4, true);
    expect(() => decodeDetailUV(count, 3)).toThrow();
  });
  it.each([NaN, Infinity, -0.1, 1.1])('rejects invalid coordinate %s', (value) => {
    const bytes = fixture();
    new DataView(bytes).setFloat32(8, value, true);
    expect(() => decodeDetailUV(bytes, 3)).toThrow();
  });
});
