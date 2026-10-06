#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Renders every registered colony-skin material on a lit sphere, through the
// shared GLSL under WebGL2 in headless Chromium, for quick iteration on
// libgag/shaders/skin-material.glsl before a native build.
//
//   cd platform/apps/web && node ../../../tools/skins/material_spheres.mjs out.png [RRGGBB]
//
// Runs from the web app so Playwright's Chromium resolves; software WebGL
// (swiftshader) keeps the result independent of the host GPU.
import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const { chromium } = createRequire(join(process.cwd(), "package.json"))(
  "@playwright/test",
);
const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const glsl = readFileSync(
  join(root, "libgag/shaders/skin-material.glsl"),
  "utf8",
);
const registry = JSON.parse(
  readFileSync(join(root, "libgag/shaders/skin-materials.json"), "utf8"),
);
const [output = "material-spheres.png", tint = "ed9252"] =
  process.argv.slice(2);

const browser = await chromium.launch({
  args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
});
const page = await browser.newPage({
  viewport: { width: 1600, height: 1400 },
  deviceScaleFactor: 1,
});
await page.setContent('<canvas id="sheet"></canvas>');
const error = await page.evaluate(
  ({ glsl, registry, tint }) => {
    const CELL = 200,
      COLUMNS = 6,
      materials = registry.materials,
      rows = Math.ceil(materials.length / COLUMNS);
    const canvas = document.getElementById("sheet");
    canvas.width = CELL * COLUMNS;
    canvas.height = CELL * rows;
    const gl = canvas.getContext("webgl2", {
      antialias: true,
      preserveDrawingBuffer: true,
    });
    if (!gl) return "WebGL2 is unavailable";
    const program = gl.createProgram();
    for (const [kind, source] of [
      [
        gl.VERTEX_SHADER,
        "#version 300 es\nin vec2 p;void main(){gl_Position=vec4(p,0.,1.);}",
      ],
      [
        gl.FRAGMENT_SHADER,
        `#version 300 es\nprecision highp float;out vec4 color;uniform vec3 tint;uniform vec2 cell;uniform float id;uniform float shell;\n${glsl}\nvoid main(){vec2 q=(gl_FragCoord.xy-cell-vec2(100.,112.))/(72.*(1.+shell*.2));float r=dot(q,q);if(r>1.)discard;vec3 n=vec3(q,sqrt(1.-r));vec4 shaded=skinShade(tint,id,n,(q+1.)*.5,shell);if(shaded.a<.5)discard;color=vec4(shaded.rgb,1.);}`,
      ],
    ]) {
      const shader = gl.createShader(kind);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        return gl.getShaderInfoLog(shader);
      gl.attachShader(program, shader);
    }
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      return gl.getProgramInfoLog(program);
    gl.useProgram(program);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 3, -1, -1, 3]),
      gl.STATIC_DRAW,
    );
    const position = gl.getAttribLocation(program, "p");
    gl.enableVertexAttribArray(position);
    gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
    gl.uniform3f(
      gl.getUniformLocation(program, "tint"),
      parseInt(tint.slice(0, 2), 16) / 255,
      parseInt(tint.slice(2, 4), 16) / 255,
      parseInt(tint.slice(4, 6), 16) / 255,
    );
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.3, 0.3, 0.33, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.SCISSOR_TEST);
    const cell = gl.getUniformLocation(program, "cell"),
      id = gl.getUniformLocation(program, "id"),
      shell = gl.getUniformLocation(program, "shell");
    materials.forEach((material, index) => {
      const x = (index % COLUMNS) * CELL,
        y = canvas.height - (Math.floor(index / COLUMNS) + 1) * CELL;
      gl.scissor(x, y, CELL, CELL);
      gl.uniform2f(cell, x, y);
      gl.uniform1f(id, material.id);
      const passes = material.shells ? registry.shells : 0;
      for (let k = 0; k <= passes; k++) {
        gl.uniform1f(shell, k / registry.shells);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
    });
    const labelled = document.createElement("canvas");
    labelled.width = canvas.width;
    labelled.height = canvas.height;
    const context = labelled.getContext("2d");
    context.drawImage(canvas, 0, 0);
    context.font = "14px sans-serif";
    context.fillStyle = "#fff";
    materials.forEach((material, index) =>
      context.fillText(
        `${material.id} ${material.name}`,
        (index % COLUMNS) * CELL + 8,
        Math.floor(index / COLUMNS) * CELL + CELL - 8,
      ),
    );
    labelled.id = "sheet";
    canvas.replaceWith(labelled);
    return "";
  },
  { glsl, registry, tint },
);
if (error) {
  console.error(error);
  process.exitCode = 1;
} else {
  await page.locator("#sheet").screenshot({ path: output });
  console.log("wrote " + output);
}
await browser.close();
