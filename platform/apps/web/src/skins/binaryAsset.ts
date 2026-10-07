// SPDX-License-Identifier: GPL-3.0-or-later
/* Bounded reading and the limits the GSR1 and GSB1 decoders share with
 * libgag/src/SkinAssetReader.h. Validation arithmetic is binary64; limits on
 * serialized scalars use their binary32 representation, including endpoints,
 * so every decoder accepts exactly the same bytes. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { determinant3 } from './affine.ts';

/** Every unit clip holds 256 frames: eight headings of 32 animation phases. */
export const CLIP_FRAMES = 256;
export const MAX_ASSET_BYTES = 16 * 1024 * 1024;
export const MAX_VERTICES = 8192;
export const MAX_INDICES = 49152;
export const MAX_CLIPS = 8;
export const MAX_LOGICAL_SIZE = 128;
export const MAX_SCALAR = 10000;
export const TOLERANCE = 0.0001;
export const MAX_HEADING = Math.fround(6.283186);

export function requireAsset(ok: boolean, message: string): asserts ok {
  if (!ok) throw new Error(message);
}

export class ByteReader {
  private readonly data: DataView;
  private readonly label: string;
  private at = 0;
  /** `label` names the format in error messages, e.g. "rig". */
  constructor(bytes: ArrayBuffer, label: string) {
    this.data = new DataView(bytes);
    this.label = label;
  }
  get offset() {
    return this.at;
  }
  get remaining() {
    return this.data.byteLength - this.at;
  }
  u32(): number {
    requireAsset(this.remaining >= 4, `Truncated ${this.label} payload`);
    const value = this.data.getUint32(this.at, true);
    this.at += 4;
    return value;
  }
  i16(): number {
    requireAsset(this.remaining >= 2, `Truncated ${this.label} payload`);
    const value = this.data.getInt16(this.at, true);
    this.at += 2;
    return value;
  }
  f32(limit = MAX_SCALAR): number {
    requireAsset(this.remaining >= 4, `Truncated ${this.label} payload`);
    const value = this.data.getFloat32(this.at, true);
    this.at += 4;
    requireAsset(
      Number.isFinite(value) && Math.abs(value) <= limit,
      `Invalid ${this.label} scalar`,
    );
    return value;
  }
  f32s(count: number): number[] {
    return Array.from({ length: count }, () => this.f32());
  }
  assertConsumed(): void {
    requireAsset(this.remaining === 0, `Trailing ${this.label} data`);
  }
}

/** Every clip carries its orthographic camera: an affine, invertible
 * model-to-clip matrix and an orthonormal model-to-camera matrix for normals. */
export type ClipCamera = { modelToClip: readonly number[]; normalToCamera: readonly number[] };
export function readClipCamera(reader: ByteReader, label: string): ClipCamera {
  const m = reader.f32s(16);
  requireAsset(m[12] === 0 && m[13] === 0 && m[14] === 0 && m[15] === 1, `Invalid ${label} camera`);
  requireAsset(Math.abs(determinant3(m)) > 1e-12, `Singular ${label} camera`);
  const n = reader.f32s(9);
  for (let i = 0; i < 3; i++)
    for (let j = 0; j < 3; j++) {
      let dot = 0;
      for (let k = 0; k < 3; k++) dot += n[i * 3 + k]! * n[j * 3 + k]!;
      requireAsset(Math.abs(dot - (i === j ? 1 : 0)) < TOLERANCE, `Invalid ${label} normal camera`);
    }
  return { modelToClip: Object.freeze(m), normalToCamera: Object.freeze(n) };
}

/** Header counts every unit asset shares: vertices, triangle indices, clips and
 * the logical sprite size of the clip camera. */
export function requireGeometryCounts(
  count: number,
  indexCount: number,
  clipCount: number,
  logicalSize: number,
  label: string,
): void {
  requireAsset(
    count >= 3 &&
      count <= MAX_VERTICES &&
      indexCount >= 3 &&
      indexCount <= MAX_INDICES &&
      indexCount % 3 === 0 &&
      clipCount >= 1 &&
      clipCount <= MAX_CLIPS &&
      logicalSize >= 1 &&
      logicalSize <= MAX_LOGICAL_SIZE,
    `Invalid ${label} dimensions`,
  );
}
