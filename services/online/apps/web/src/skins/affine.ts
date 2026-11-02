// SPDX-License-Identifier: GPL-3.0-or-later
/* Row-major affine matrix helpers shared by the skin decoders and Studio projection. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
/** Sixteen row-major values; the last row is 0 0 0 1 for every skin camera. */
export type Mat4 = readonly number[];
/** Nine row-major values. */
export type Mat3 = readonly number[];
export type Vec3 = [number, number, number];

export function determinant3(m: Mat4): number {
  return (
    m[0]! * (m[5]! * m[10]! - m[6]! * m[9]!) -
    m[1]! * (m[4]! * m[10]! - m[6]! * m[8]!) +
    m[2]! * (m[4]! * m[9]! - m[5]! * m[8]!)
  );
}
/** Inverse of an affine matrix whose 3x3 block is not singular. */
export function invertAffine(m: Mat4): number[] {
  const determinant = determinant3(m);
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
  return inverse;
}
export function transposed3(n: Mat3): number[] {
  return Array.from({ length: 9 }, (_, k) => n[(k % 3) * 3 + Math.floor(k / 3)]!);
}
export function transformPoint(m: Mat4, x: number, y: number, z: number): Vec3 {
  return [
    m[0]! * x + m[1]! * y + m[2]! * z + m[3]!,
    m[4]! * x + m[5]! * y + m[6]! * z + m[7]!,
    m[8]! * x + m[9]! * y + m[10]! * z + m[11]!,
  ];
}
export function transformNormal(n: Mat3, x: number, y: number, z: number): Vec3 {
  return [
    n[0]! * x + n[1]! * y + n[2]! * z,
    n[3]! * x + n[4]! * y + n[5]! * z,
    n[6]! * x + n[7]! * y + n[8]! * z,
  ];
}
/** Three values at `at` become unit length, or +Z when they cancel to zero:
 * the rule the native evaluators and the deformation shader share. */
export function normalizeOrUp(out: Float32Array, at: number): void {
  const length = Math.hypot(out[at]!, out[at + 1]!, out[at + 2]!);
  if (length < 1e-8) {
    out[at] = 0;
    out[at + 1] = 0;
    out[at + 2] = 1;
  } else {
    out[at] = out[at]! / length;
    out[at + 1] = out[at + 1]! / length;
    out[at + 2] = out[at + 2]! / length;
  }
}
export async function sha256Hex(bytes: ArrayBuffer): Promise<string> {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
}
