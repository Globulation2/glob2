// SPDX-License-Identifier: GPL-3.0-or-later
/* GSB1 contract; keep in agreement with libgag/src/SkinShapeModel.cpp.
 *
 * A blend-shape clip stores a mean mesh plus shape vectors (int16 deltas with
 * one float scale each) for positions and, separately, normals. A frame is
 * the mean plus its coefficients times the shapes, turned about model Z by
 * the frame's heading and taken into the clip's orthographic camera. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { normalizeOrUp } from './affine.ts';
import {
  ByteReader,
  CLIP_FRAMES,
  MAX_ASSET_BYTES,
  MAX_HEADING,
  readClipCamera,
  requireAsset,
  requireGeometryCounts,
  type ClipCamera,
} from './binaryAsset.ts';

export type ShapeClip = Readonly<
  ClipCamera & {
    id: number;
    headings: readonly number[];
    coefficients: Float32Array; // 256 x shapes, frame-major
    normalCoefficients: Float32Array; // 256 x normalShapes, frame-major
  }
>;
export type ShapeModel = Readonly<{
  count: number;
  logicalSize: number;
  shapes: number;
  normalShapes: number;
  uv: Float32Array;
  indices: Uint32Array;
  mean: Float32Array; // xyz per vertex, model space
  normalMean: Float32Array;
  shapeScales: Float32Array;
  shapeDeltas: Int16Array; // shape-major, xyz per vertex
  normalScales: Float32Array;
  normalDeltas: Int16Array;
  clips: readonly ShapeClip[];
}>;
const HEADER_BYTES = 32;
const MAX_SHAPES = 128;

export function decodeShapes(bytes: ArrayBuffer): ShapeModel {
  requireAsset(
    bytes.byteLength >= HEADER_BYTES && bytes.byteLength <= MAX_ASSET_BYTES,
    'Invalid shape size',
  );
  const reader = new ByteReader(bytes, 'shape');
  requireAsset(reader.u32() === 0x31425347, 'Unsupported shape format');
  const count = reader.u32(),
    indexCount = reader.u32(),
    shapes = reader.u32(),
    normalShapes = reader.u32(),
    clipCount = reader.u32(),
    logicalSize = reader.u32(),
    payload = reader.u32();
  requireGeometryCounts(count, indexCount, clipCount, logicalSize, 'shape');
  requireAsset(
    shapes >= 1 &&
      shapes <= MAX_SHAPES &&
      normalShapes >= 1 &&
      normalShapes <= MAX_SHAPES &&
      payload === bytes.byteLength - HEADER_BYTES,
    'Invalid shape dimensions',
  );
  const expected =
    HEADER_BYTES +
    count * 8 +
    indexCount * 4 +
    count * 24 +
    (shapes + normalShapes) * (4 + count * 6) +
    clipCount * (8 + 64 + 36 + CLIP_FRAMES * 4 + CLIP_FRAMES * 4 * (shapes + normalShapes));
  requireAsset(expected === bytes.byteLength, 'Shape asset length mismatch');
  const uv = new Float32Array(count * 2);
  for (let i = 0; i < uv.length; i++) {
    uv[i] = reader.f32();
    requireAsset(uv[i]! >= 0 && uv[i]! <= 1, 'Invalid shape UV');
  }
  const indices = new Uint32Array(indexCount);
  for (let i = 0; i < indexCount; i++) {
    indices[i] = reader.u32();
    requireAsset(indices[i]! < count, 'Invalid shape index');
  }
  const mean = new Float32Array(count * 3);
  for (let i = 0; i < mean.length; i++) mean[i] = reader.f32();
  const normalMean = new Float32Array(count * 3);
  for (let i = 0; i < normalMean.length; i++) normalMean[i] = reader.f32();
  const readShapes = (total: number) => {
    const scales = new Float32Array(total);
    const deltas = new Int16Array(total * count * 3);
    for (let s = 0; s < total; s++) {
      scales[s] = reader.f32();
      requireAsset(scales[s]! > 0, 'Invalid shape scale');
      for (let k = 0; k < count * 3; k++) deltas[s * count * 3 + k] = reader.i16();
    }
    return { scales, deltas };
  };
  const positionShapes = readShapes(shapes);
  const normalShapeData = readShapes(normalShapes);
  const clips: ShapeClip[] = [];
  const ids = new Set<number>();
  for (let c = 0; c < clipCount; c++) {
    const id = reader.u32();
    requireAsset(!ids.has(id), 'Duplicate shape clip');
    ids.add(id);
    requireAsset(reader.u32() === CLIP_FRAMES, 'Unsupported shape clip frames');
    const camera = readClipCamera(reader, 'shape');
    const headings = reader.f32s(CLIP_FRAMES);
    requireAsset(
      headings.every((h) => Math.abs(h) <= MAX_HEADING),
      'Invalid shape heading',
    );
    const coefficients = new Float32Array(CLIP_FRAMES * shapes);
    for (let i = 0; i < coefficients.length; i++) coefficients[i] = reader.f32();
    const normalCoefficients = new Float32Array(CLIP_FRAMES * normalShapes);
    for (let i = 0; i < normalCoefficients.length; i++) normalCoefficients[i] = reader.f32();
    clips.push(
      Object.freeze({
        id,
        ...camera,
        headings: Object.freeze(headings),
        coefficients,
        normalCoefficients,
      }),
    );
  }
  reader.assertConsumed();
  return Object.freeze({
    count,
    logicalSize,
    shapes,
    normalShapes,
    uv,
    indices,
    mean,
    normalMean,
    shapeScales: positionShapes.scales,
    shapeDeltas: positionShapes.deltas,
    normalScales: normalShapeData.scales,
    normalDeltas: normalShapeData.deltas,
    clips: Object.freeze(clips),
  });
}
/** Evaluates one frame as xyz, normal xyz per vertex: in camera (clip) space
 * by default, or in model space with the heading applied. Reuses `output`
 * when it has the right length. */
export function evaluateShapes(
  model: ShapeModel,
  clip: number,
  frame: number,
  cameraSpace = true,
  output?: Float32Array,
): Float32Array {
  const animation = model.clips[clip];
  requireAsset(
    animation !== undefined && Number.isInteger(frame) && frame >= 0 && frame < CLIP_FRAMES,
    'Invalid shape frame',
  );
  const count = model.count;
  const out = output?.length === count * 6 ? output : new Float32Array(count * 6);
  for (let v = 0; v < count; v++)
    for (let k = 0; k < 3; k++) {
      out[v * 6 + k] = model.mean[v * 3 + k]!;
      out[v * 6 + 3 + k] = model.normalMean[v * 3 + k]!;
    }
  const blend = (
    total: number,
    coefficients: Float32Array,
    scales: Float32Array,
    deltas: Int16Array,
    component: number,
  ) => {
    for (let s = 0; s < total; s++) {
      const scale = coefficients[frame * total + s]! * scales[s]!;
      const base = s * count * 3;
      for (let v = 0; v < count; v++)
        for (let k = 0; k < 3; k++) {
          const at = v * 6 + component + k;
          out[at] = out[at]! + scale * deltas[base + v * 3 + k]!;
        }
    }
  };
  blend(model.shapes, animation.coefficients, model.shapeScales, model.shapeDeltas, 0);
  blend(
    model.normalShapes,
    animation.normalCoefficients,
    model.normalScales,
    model.normalDeltas,
    3,
  );
  const heading = animation.headings[frame]!;
  const c = Math.cos(heading),
    s = Math.sin(heading);
  const m = animation.modelToClip,
    n = animation.normalToCamera;
  for (let v = 0; v < count; v++) {
    const o = v * 6;
    // Single-precision intermediates mirror the native evaluator.
    const x = Math.fround(c * out[o]! - s * out[o + 1]!),
      y = Math.fround(s * out[o]! + c * out[o + 1]!),
      z = out[o + 2]!;
    const nx = Math.fround(c * out[o + 3]! - s * out[o + 4]!),
      ny = Math.fround(s * out[o + 3]! + c * out[o + 4]!),
      nz = out[o + 5]!;
    if (cameraSpace) {
      for (let row = 0; row < 3; row++)
        out[o + row] =
          m[row * 4]! * x + m[row * 4 + 1]! * y + m[row * 4 + 2]! * z + m[row * 4 + 3]!;
      for (let row = 0; row < 3; row++)
        out[o + 3 + row] = n[row * 3]! * nx + n[row * 3 + 1]! * ny + n[row * 3 + 2]! * nz;
    } else {
      out[o] = x;
      out[o + 1] = y;
      out[o + 2] = z;
      out[o + 3] = nx;
      out[o + 4] = ny;
      out[o + 5] = nz;
    }
    normalizeOrUp(out, o + 3);
  }
  return out;
}
