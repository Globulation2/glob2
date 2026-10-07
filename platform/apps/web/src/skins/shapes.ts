// SPDX-License-Identifier: GPL-3.0-or-later
/* GSB1 contract; keep in agreement with libgag/src/SkinShapeModel.cpp. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
type Values = readonly number[];
export type ShapeClip = Readonly<{
  id: number;
  modelToClip: Values;
  normalToCamera: Values;
  headings: Values;
  coefficients: Float32Array; // 256 x shapes, frame-major
  normalCoefficients: Float32Array; // 256 x normalShapes, frame-major
}>;
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
const FRAMES = 256;
const tolerance = 0.0001;
const maximumHeading = Math.fround(6.283186);
function requireShapes(ok: boolean, message: string): asserts ok {
  if (!ok) throw new Error(message);
}
export function decodeShapes(bytes: ArrayBuffer): ShapeModel {
  requireShapes(
    bytes.byteLength >= 32 && bytes.byteLength <= 16 * 1024 * 1024,
    'Invalid shape size',
  );
  const data = new DataView(bytes);
  let at = 0;
  const word = () => {
    requireShapes(at + 4 <= bytes.byteLength, 'Truncated shape payload');
    const value = data.getUint32(at, true);
    at += 4;
    return value;
  };
  const scalar = () => {
    requireShapes(at + 4 <= bytes.byteLength, 'Truncated shape payload');
    const value = data.getFloat32(at, true);
    at += 4;
    requireShapes(Number.isFinite(value) && Math.abs(value) <= 10000, 'Invalid shape scalar');
    return value;
  };
  requireShapes(word() === 0x31425347, 'Unsupported shape format');
  const count = word(),
    indexCount = word(),
    shapes = word(),
    normalShapes = word(),
    clipCount = word(),
    logicalSize = word(),
    payload = word();
  requireShapes(
    count >= 3 &&
      count <= 8192 &&
      indexCount >= 3 &&
      indexCount <= 49152 &&
      indexCount % 3 === 0 &&
      shapes >= 1 &&
      shapes <= 128 &&
      normalShapes >= 1 &&
      normalShapes <= 128 &&
      clipCount >= 1 &&
      clipCount <= 8 &&
      logicalSize >= 1 &&
      logicalSize <= 128 &&
      payload === bytes.byteLength - 32,
    'Invalid shape dimensions',
  );
  const expected =
    32 +
    count * 8 +
    indexCount * 4 +
    count * 24 +
    (shapes + normalShapes) * (4 + count * 6) +
    clipCount * (8 + 64 + 36 + FRAMES * 4 + FRAMES * 4 * (shapes + normalShapes));
  requireShapes(expected === bytes.byteLength, 'Shape asset length mismatch');
  const uv = new Float32Array(count * 2);
  for (let i = 0; i < uv.length; i++) {
    uv[i] = scalar();
    requireShapes(uv[i]! >= 0 && uv[i]! <= 1, 'Invalid shape UV');
  }
  const indices = new Uint32Array(indexCount);
  for (let i = 0; i < indexCount; i++) {
    indices[i] = word();
    requireShapes(indices[i]! < count, 'Invalid shape index');
  }
  const mean = new Float32Array(count * 3);
  for (let i = 0; i < mean.length; i++) mean[i] = scalar();
  const normalMean = new Float32Array(count * 3);
  for (let i = 0; i < normalMean.length; i++) normalMean[i] = scalar();
  const readShapes = (total: number) => {
    const scales = new Float32Array(total);
    const deltas = new Int16Array(total * count * 3);
    for (let s = 0; s < total; s++) {
      scales[s] = scalar();
      requireShapes(scales[s]! > 0, 'Invalid shape scale');
      for (let k = 0; k < count * 3; k++) {
        requireShapes(at + 2 <= bytes.byteLength, 'Truncated shape payload');
        deltas[s * count * 3 + k] = data.getInt16(at, true);
        at += 2;
      }
    }
    return { scales, deltas };
  };
  const positionShapes = readShapes(shapes);
  const normalShapeData = readShapes(normalShapes);
  const clips: ShapeClip[] = [];
  const ids = new Set<number>();
  for (let c = 0; c < clipCount; c++) {
    const id = word();
    requireShapes(!ids.has(id), 'Duplicate shape clip');
    ids.add(id);
    requireShapes(word() === FRAMES, 'Unsupported shape clip frames');
    const m = Array.from({ length: 16 }, scalar);
    requireShapes(m[12] === 0 && m[13] === 0 && m[14] === 0 && m[15] === 1, 'Invalid shape camera');
    const determinant =
      m[0]! * (m[5]! * m[10]! - m[6]! * m[9]!) -
      m[1]! * (m[4]! * m[10]! - m[6]! * m[8]!) +
      m[2]! * (m[4]! * m[9]! - m[5]! * m[8]!);
    requireShapes(Math.abs(determinant) > 1e-12, 'Invalid shape camera');
    const n = Array.from({ length: 9 }, scalar);
    for (let i = 0; i < 3; i++)
      for (let j = 0; j < 3; j++) {
        let dot = 0;
        for (let k = 0; k < 3; k++) dot += n[i * 3 + k]! * n[j * 3 + k]!;
        requireShapes(Math.abs(dot - (i === j ? 1 : 0)) < tolerance, 'Invalid shape normal camera');
      }
    const headings = Array.from({ length: FRAMES }, scalar);
    requireShapes(
      headings.every((h) => Math.abs(h) <= maximumHeading),
      'Invalid shape heading',
    );
    const coefficients = new Float32Array(FRAMES * shapes);
    for (let i = 0; i < coefficients.length; i++) coefficients[i] = scalar();
    const normalCoefficients = new Float32Array(FRAMES * normalShapes);
    for (let i = 0; i < normalCoefficients.length; i++) normalCoefficients[i] = scalar();
    clips.push(
      Object.freeze({
        id,
        modelToClip: Object.freeze(m),
        normalToCamera: Object.freeze(n),
        headings: Object.freeze(headings),
        coefficients,
        normalCoefficients,
      }),
    );
  }
  requireShapes(at === bytes.byteLength, 'Trailing shape data');
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
/** Evaluates one frame: xyz, normal xyz per vertex, in clip space unless told otherwise. */
export function evaluateShapes(
  model: ShapeModel,
  clip: number,
  frame: number,
  cameraSpace = true,
  output?: Float32Array,
): Float32Array {
  const animation = model.clips[clip];
  requireShapes(
    animation !== undefined && Number.isInteger(frame) && frame >= 0 && frame < FRAMES,
    'Invalid shape frame',
  );
  const count = model.count;
  const out = output?.length === count * 6 ? output : new Float32Array(count * 6);
  for (let v = 0; v < count; v++)
    for (let k = 0; k < 3; k++) {
      out[v * 6 + k] = model.mean[v * 3 + k]!;
      out[v * 6 + 3 + k] = model.normalMean[v * 3 + k]!;
    }
  for (let s = 0; s < model.shapes; s++) {
    const scale = animation.coefficients[frame * model.shapes + s]! * model.shapeScales[s]!;
    const base = s * count * 3;
    for (let v = 0; v < count; v++)
      for (let k = 0; k < 3; k++)
        out[v * 6 + k] = out[v * 6 + k]! + scale * model.shapeDeltas[base + v * 3 + k]!;
  }
  for (let s = 0; s < model.normalShapes; s++) {
    const scale =
      animation.normalCoefficients[frame * model.normalShapes + s]! * model.normalScales[s]!;
    const base = s * count * 3;
    for (let v = 0; v < count; v++)
      for (let k = 0; k < 3; k++)
        out[v * 6 + 3 + k] = out[v * 6 + 3 + k]! + scale * model.normalDeltas[base + v * 3 + k]!;
  }
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
    const length = Math.hypot(out[o + 3]!, out[o + 4]!, out[o + 5]!);
    if (length < 1e-8) {
      out[o + 3] = 0;
      out[o + 4] = 0;
      out[o + 5] = 1;
    } else for (let k = 0; k < 3; k++) out[o + 3 + k] = out[o + 3 + k]! / length;
  }
  return out;
}
