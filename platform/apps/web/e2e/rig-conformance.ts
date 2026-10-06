// SPDX-License-Identifier: GPL-3.0-or-later
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import ts from 'typescript';
import type * as RigEvaluator from '../src/skins/rig.ts';

const root = new URL('../../../../', import.meta.url);
const read = (path: string) => readFileSync(new URL(path, root));
// Transpile the production evaluator, without maintaining a browser-only copy.
const evaluator = ts.transpileModule(read('platform/apps/web/src/skins/rig.ts').toString(), {
  compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.ESNext },
}).outputText;
const shader = read('libgag/include/SkinDeformation.h')
  .toString()
  .match(/R"GLSL\(([\s\S]*?)\)GLSL"/u)?.[1];
if (!shader) throw new Error('Production deformation shader was not found');
const fixture = JSON.parse(read('test/fixtures/skins/rig.json').toString()) as {
  hex: string;
  affineCamera: { hex: string };
};
const assets = [
  { name: 'analytic hierarchy and weighted normals', bytes: Buffer.from(fixture.hex, 'hex') },
  {
    name: 'translated camera with rounded influence sums',
    bytes: Buffer.from(fixture.affineCamera.hex, 'hex'),
    // Zero rest positions isolate camera translation, and 10000 / 1.25 is exact.
    // Use a focused tolerance so a rounded homogeneous weight sum cannot hide
    // inside the ordinary mesh agreement budget.
    positionTolerance: 0.00001,
  },
  { name: 'installed worker', bytes: read('data/skins/colony-v1/worker-walk.gsr') },
];

for (const asset of assets) {
  test(`${asset.name}: every clip and frame agrees with the production shader`, async ({
    page,
  }, info) => {
    const result = await page.evaluate(
      async ({ evaluator, shader, bytes }) => {
        const { decodeRig, rigFramePalette, evaluateRig } = (await import(
          'data:text/javascript;base64,' + btoa(evaluator)
        )) as typeof RigEvaluator;
        const model = decodeRig(new Uint8Array(bytes).buffer);
        // The platform tsconfig intentionally omits global DOM types: adding
        // lib.dom here also changes Node fetch overloads in unrelated API tests.
        // @ts-expect-error This callback executes in Playwright's browser realm.
        const gl = document.createElement('canvas').getContext('webgl2');
        if (!gl) throw new Error('WebGL2 unavailable: this backend remains unverified');
        const compile = (type: number, source: string) => {
          const shader = gl.createShader(type);
          if (!shader) throw new Error('Could not create shader');
          gl.shaderSource(shader, source);
          gl.compileShader(shader);
          if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
            throw new Error(gl.getShaderInfoLog(shader) ?? 'Shader compilation failed');
          return shader;
        };
        const vertex = compile(
          gl.VERTEX_SHADER,
          '#version 300 es\nprecision highp float;\n' +
            'in vec3 position;in vec3 surfaceNormal;in vec2 texcoord;in vec4 joints;in vec4 weights;' +
            'out vec2 uv;out vec3 normal;\n' +
            shader,
        );
        const fragment = compile(
          gl.FRAGMENT_SHADER,
          '#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(1.0);}',
        );
        const program = gl.createProgram();
        if (!program) throw new Error('Could not create shader program');
        gl.attachShader(program, vertex);
        gl.attachShader(program, fragment);
        ['position', 'surfaceNormal', 'texcoord', 'joints', 'weights'].forEach((name, i) =>
          gl.bindAttribLocation(program, i, name),
        );
        // Capture numerical outputs directly, independent of pixel coverage or materials.
        gl.transformFeedbackVaryings(program, ['gl_Position', 'normal'], gl.SEPARATE_ATTRIBS);
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS))
          throw new Error(gl.getProgramInfoLog(program) ?? 'Shader linking failed');
        gl.useProgram(program);

        const geometry = new Float32Array(model.count * 16);
        for (let v = 0; v < model.count; v++) {
          geometry.set(model.rest.slice(v * 6, v * 6 + 6), v * 16);
          geometry.set(model.uv.slice(v * 2, v * 2 + 2), v * 16 + 6);
          geometry.set(model.influences[v]!.bones, v * 16 + 8);
          geometry.set(model.influences[v]!.weights, v * 16 + 12);
        }
        const geometryBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, geometryBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, geometry, gl.STATIC_DRAW);
        const sizes = [3, 3, 2, 4, 4],
          offsets = [0, 3, 6, 8, 12];
        for (let i = 0; i < sizes.length; i++) {
          gl.enableVertexAttribArray(i);
          gl.vertexAttribPointer(i, sizes[i]!, gl.FLOAT, false, 64, offsets[i]! * 4);
        }
        const outputs = [new Float32Array(model.count * 4), new Float32Array(model.count * 3)];
        const feedback = gl.createTransformFeedback();
        gl.bindTransformFeedback(gl.TRANSFORM_FEEDBACK, feedback);
        const buffers = outputs.map((output, i) => {
          const buffer = gl.createBuffer();
          gl.bindBuffer(gl.TRANSFORM_FEEDBACK_BUFFER, buffer);
          gl.bufferData(gl.TRANSFORM_FEEDBACK_BUFFER, output.byteLength, gl.STREAM_READ);
          gl.bindBufferBase(gl.TRANSFORM_FEEDBACK_BUFFER, i, buffer);
          return buffer;
        });
        const transpose = (matrix: readonly number[], n: number) =>
          new Float32Array(matrix.map((_, i) => matrix[(i % n) * n + Math.floor(i / n)]!));
        const bones = gl.getUniformLocation(program, 'bones[0]');
        const view = gl.getUniformLocation(program, 'view');
        const normalView = gl.getUniformLocation(program, 'normalView');
        const matrices = new Float32Array(model.bones.length * 16);
        gl.enable(gl.RASTERIZER_DISCARD);
        let positionError = 0,
          normalError = 0,
          checkedPoses = 0;
        for (let clip = 0; clip < model.clips.length; clip++) {
          gl.uniformMatrix4fv(view, false, transpose(model.clips[clip]!.modelToClip, 4));
          gl.uniformMatrix3fv(normalView, false, transpose(model.clips[clip]!.normalToCamera, 3));
          for (let sample = 0; sample < 256; sample++) {
            const request = { model, clip, sample };
            rigFramePalette(request).positions.forEach((matrix, i) =>
              matrices.set(transpose(matrix, 4), i * 16),
            );
            gl.uniformMatrix4fv(bones, false, matrices);
            gl.beginTransformFeedback(gl.POINTS);
            gl.drawArrays(gl.POINTS, 0, model.count);
            gl.endTransformFeedback();
            buffers.forEach((buffer, i) => {
              gl.bindBuffer(gl.TRANSFORM_FEEDBACK_BUFFER, buffer);
              gl.getBufferSubData(gl.TRANSFORM_FEEDBACK_BUFFER, 0, outputs[i]!);
            });
            if (gl.getError() !== gl.NO_ERROR)
              throw new Error(`GL capture failed at ${clip}/${sample}`);
            const cpu = evaluateRig(request);
            for (let v = 0; v < model.count; v++) {
              let normalSquared = 0;
              // Remove shader atlas padding before expressing screen-space error in logical pixels.
              const dx = ((outputs[0]![v * 4]! * 1.25 - cpu[v * 6]!) * model.logicalSize) / 2;
              const dy =
                ((outputs[0]![v * 4 + 1]! * 1.25 - cpu[v * 6 + 1]!) * model.logicalSize) / 2;
              positionError = Math.max(positionError, Math.hypot(dx, dy));
              for (let axis = 0; axis < 3; axis++)
                normalSquared += (outputs[1]![v * 3 + axis]! - cpu[v * 6 + 3 + axis]!) ** 2;
              normalError = Math.max(normalError, Math.sqrt(normalSquared));
            }
            checkedPoses++;
          }
        }
        const debug = gl.getExtension('WEBGL_debug_renderer_info');
        const renderer = String(
          gl.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : gl.RENDERER),
        );
        gl.disable(gl.RASTERIZER_DISCARD);
        buffers.forEach((buffer) => gl.deleteBuffer(buffer));
        gl.deleteBuffer(geometryBuffer);
        gl.deleteTransformFeedback(feedback);
        gl.deleteProgram(program);
        gl.deleteShader(vertex);
        gl.deleteShader(fragment);
        return { positionError, normalError, checkedPoses, clips: model.clips.length, renderer };
      },
      { evaluator, shader, bytes: Array.from(asset.bytes) },
    );
    await info.attach('deformation-agreement.json', {
      body: JSON.stringify({ asset: asset.name, ...result }, null, 2),
      contentType: 'application/json',
    });
    expect(result.checkedPoses).toBe(result.clips * 256);
    expect(result.positionError).toBeLessThanOrEqual(asset.positionTolerance ?? 0.05);
    expect(result.normalError).toBeLessThanOrEqual(0.001);
  });
}
