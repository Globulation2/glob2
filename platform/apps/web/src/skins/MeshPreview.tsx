/* Indexed geometry is bounded by decode before rendering or hit testing. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import type { SwarmMeshId } from '@glob2/protocol';
import { ATLAS_SIZE, type Model } from './atlas.ts';
import { swarmModel } from './swarmShapes.ts';

// Kept identical to libgag/src/GraphicContextSkinMesh.cpp (test_skin_shader_parity.py).
const SKIN_MATERIAL_GLSL = `
// BEGIN skin-material
float skinHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float skinNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 s = f * f * (3.0 - 2.0 * f);
  return mix(mix(skinHash(i), skinHash(i + vec2(1.0, 0.0)), s.x),
             mix(skinHash(i + vec2(0.0, 1.0)), skinHash(i + vec2(1.0, 1.0)), s.x), s.y);
}
// The original glob material bumps its normals with Stucci noise (norfac 5).
float skinStucci(vec2 p) { return skinNoise(p) + 0.5 * skinNoise(p * 2.03 + 17.0); }
vec3 skinBump(vec3 n, vec2 uv, float amount, float frequency) {
  float e = 0.25 / frequency;
  float h = skinStucci(uv * frequency);
  float gu = (skinStucci((uv + vec2(e, 0.0)) * frequency) - h) / e;
  float gv = (skinStucci((uv + vec2(0.0, e)) * frequency) - h) / e;
  // Express the UV height gradient in screen directions, independent of resolution.
  vec2 du = vec2(dFdx(uv.x), dFdy(uv.x));
  vec2 dv = vec2(dFdx(uv.y), dFdy(uv.y));
  float density = max(0.5 * (length(du) + length(dv)), 1e-6);
  vec2 g = amount * (gu * du + gv * dv) / density;
  // Stretched UV regions exaggerate the gradient; keep the tilt bounded.
  g *= min(1.0, 0.7 / max(length(g), 1e-6));
  return normalize(n - vec3(g, 0.0));
}
// Material ids come from the skin's material map: 0 classic glossy,
// 1 matte, 2 metallic, 3 hairy (shell fur adds strands in extra passes).
vec3 skinShade(vec3 albedo, float material, vec3 surfaceNormal, vec2 uv) {
  vec3 n = normalize(surfaceNormal);
  vec3 l = normalize(vec3(-0.4, 0.7, 1.0));
  vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
  if (material < 0.5) {
    // The original glob material: Stucci-bumped body and broad white streaks
    // (specular 0.5, hardness 2), lit like the classic sprites.
    vec3 b = skinBump(n, uv, 0.08, 12.0);
    float nh = max(0.0, dot(b, h));
    return albedo * (0.24 + 0.66 * max(0.0, dot(b, l))) + vec3(0.42 * pow(nh, 4.0));
  }
  if (material < 1.5) {
    return albedo * (0.45 + 0.55 * max(0.0, dot(n, l)));
  }
  if (material < 2.5) {
    // Metal: albedo-tinted reflection of a sky/ground gradient, a tight
    // highlight and a brightening rim.
    vec3 b = skinBump(n, uv, 0.015, 20.0);
    float facing = max(0.0, b.z);
    float nh = max(0.0, dot(b, h));
    vec3 sky = mix(vec3(0.18), vec3(1.0), smoothstep(-0.6, 0.8, b.y));
    float rim = pow(1.0 - facing, 3.0);
    vec3 tint = mix(albedo, vec3(1.0), 0.3 * rim);
    return tint * (0.12 + 0.2 * max(0.0, dot(b, l)) + 0.75 * sky) + vec3(pow(nh, 40.0));
  }
  // Hairy: soft, wrapped diffuse with a light fringe.
  float wrap = max(0.0, (dot(n, l) + 0.5) / 1.5);
  float fringe = pow(1.0 - max(0.0, n.z), 2.0);
  return albedo * (0.35 + 0.65 * wrap) + albedo * 0.35 * fringe;
}
// END skin-material
`;

// The same bounded GSK1 model data and orthographic projection as the native renderer.
type Mesh = {
  count: number;
  frames: number;
  uv: Float32Array;
  indices: Uint32Array;
  poses: Float32Array;
};
function decode(bytes: ArrayBuffer): Mesh {
  if (bytes.byteLength < 20 || bytes.byteLength > 64 * 1024 * 1024)
    throw new Error('Invalid model size');
  const data = new DataView(bytes);
  if (data.getUint32(0, true) !== 0x314b5347) throw new Error('Invalid model format');
  const count = data.getUint32(4, true),
    indices = data.getUint32(8, true),
    frames = data.getUint32(12, true);
  if (
    count < 3 ||
    count > 8192 ||
    indices < 3 ||
    indices > 49152 ||
    indices % 3 ||
    ![1, 256].includes(frames) ||
    bytes.byteLength !== 20 + count * 8 + indices * 4 + frames * count * 24
  )
    throw new Error('Invalid model dimensions');
  const uv = new Float32Array(bytes, 20, count * 2),
    index = new Uint32Array(bytes, 20 + count * 8, indices);
  const poses = new Float32Array(bytes, 20 + count * 8 + indices * 4);
  if (
    index.some((v) => v >= count) ||
    uv.some((v) => !Number.isFinite(v) || v < 0 || v > 1) ||
    poses.some((v) => !Number.isFinite(v))
  )
    throw new Error('Invalid model geometry');
  return { count, frames, uv, indices: index, poses };
}
// The animated clips the preview offers for each model.
const ACTIONS: Record<Model['id'], readonly string[]> = {
  worker: ['walk', 'swim', 'harvest'],
  warrior: ['walk', 'swim', 'fight'],
  explorer: ['fly'],
  swarm: [],
};

export function MeshPreview({
  texture,
  swarmMesh,
  materials,
  materialRevision,
  model,
  onPaint,
  onStroke,
}: {
  texture: HTMLCanvasElement | null;
  swarmMesh: SwarmMeshId;
  materials: () => Uint8Array;
  materialRevision: number;
  model: Model;
  onPaint: (u: number, v: number) => void;
  onStroke: () => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const pick = useRef<(x: number, y: number) => void>(() => {});
  const paint = useRef(onPaint),
    stroke = useRef(onStroke);
  const materialMap = useRef({ map: materials, revision: materialRevision });
  useEffect(() => {
    paint.current = onPaint;
    stroke.current = onStroke;
    materialMap.current = { map: materials, revision: materialRevision };
  });
  const [phase, setPhase] = useState(0);
  const [action, setAction] = useState<string>(ACTIONS[model.id][0] ?? '');
  // Switching models starts on that model's first action; the swarm has none and
  // always previews the chosen swarm shape.
  const [shownModel, setShownModel] = useState(model.id);
  if (shownModel !== model.id) {
    setShownModel(model.id);
    setAction(ACTIONS[model.id][0] ?? '');
    setPhase(0);
  }
  const [direction, setDirection] = useState(0);
  const [animate, setAnimate] = useState(true);
  const [error, setError] = useState('');
  const controls = useRef({ direction, animate, phase });
  useEffect(() => {
    controls.current = { direction, animate, phase };
  }, [direction, animate, phase]);
  const asset = model.id === 'swarm' ? swarmModel(swarmMesh) : `${model.id}-${action}`;
  useEffect(() => {
    if (!canvas.current || !texture) return;
    const target = canvas.current;
    const gl = target.getContext('webgl2', { alpha: false, antialias: true });
    if (!gl) {
      setError('3D preview needs WebGL 2. You can still paint the texture.');
      return;
    }
    const abort = new AbortController();
    let animation = 0;
    const resources: (() => void)[] = [];
    async function start() {
      if (!gl) return;
      const response = await fetch(`/skins/models/${asset}.gsk`, { signal: abort.signal });
      if (!response.ok) throw new Error('Could not load the colony model.');
      const mesh = decode(await response.arrayBuffer());
      if (abort.signal.aborted) return;
      function shader(type: number, text: string) {
        const s = gl!.createShader(type)!;
        resources.push(() => gl!.deleteShader(s));
        gl!.shaderSource(s, text);
        gl!.compileShader(s);
        if (!gl!.getShaderParameter(s, gl!.COMPILE_STATUS))
          throw new Error('3D shader could not compile.');
        return s;
      }
      const program = gl.createProgram()!;
      resources.push(() => gl.deleteProgram(program));
      gl.attachShader(
        program,
        shader(
          gl.VERTEX_SHADER,
          `#version 300 es
        in vec3 position; in vec3 normal; in vec2 uv; out vec3 n; out vec2 tex;
        void main(){gl_Position=vec4(position.xy/1.25,position.z,1.);n=normal;tex=uv;}`,
        ),
      );
      gl.attachShader(
        program,
        shader(
          gl.FRAGMENT_SHADER,
          `#version 300 es
        precision highp float; in vec3 n; in vec2 tex; out vec4 color;
        uniform sampler2D paint; uniform sampler2D material; uniform vec2 region;
${SKIN_MATERIAL_GLSL}
        void main(){vec2 atlas=tex*.5+region;float id=floor(texture(material,atlas).r*255.+.5);
        color=vec4(skinShade(texture(paint,atlas).rgb,id,n,tex),1.);}`,
        ),
      );
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS))
        throw new Error('3D preview could not start.');
      gl.useProgram(program);
      gl.uniform1i(gl.getUniformLocation(program, 'paint'), 0);
      gl.uniform1i(gl.getUniformLocation(program, 'material'), 1);
      gl.uniform2f(
        gl.getUniformLocation(program, 'region'),
        model.x / ATLAS_SIZE,
        model.y / ATLAS_SIZE,
      );
      const buffer = (kind: number, values: ArrayBufferView) => {
        const b = gl.createBuffer()!;
        resources.push(() => gl.deleteBuffer(b));
        gl.bindBuffer(kind, b);
        gl.bufferData(kind, values, gl.STATIC_DRAW);
        return b;
      };
      const poses = buffer(gl.ARRAY_BUFFER, mesh.poses.subarray(0, mesh.count * 6));
      for (const [name, offset] of [
        ['position', 0],
        ['normal', 12],
      ] as const) {
        const loc = gl.getAttribLocation(program, name);
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 24, offset);
      }
      buffer(gl.ARRAY_BUFFER, mesh.uv);
      const uv = gl.getAttribLocation(program, 'uv');
      gl.enableVertexAttribArray(uv);
      gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 0, 0);
      buffer(gl.ELEMENT_ARRAY_BUFFER, mesh.indices);
      const texture2d = (unit: number, filter: number) => {
        const t = gl.createTexture()!;
        resources.push(() => gl.deleteTexture(t));
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return t;
      };
      const colour = texture2d(0, gl.LINEAR);
      // Material ids must never blend between neighbours.
      const material = texture2d(1, gl.NEAREST);
      let uploadedMaterials = -1;
      gl.enable(gl.DEPTH_TEST);
      gl.clearColor(0.09, 0.12, 0.14, 1);
      let frame = 0;
      pick.current = (x, y) => {
        let depth = Infinity,
          hit: [number, number] | undefined;
        for (let i = 0; i < mesh.indices.length; i += 3) {
          const ids = [mesh.indices[i]!, mesh.indices[i + 1]!, mesh.indices[i + 2]!];
          const p = ids.map((id) =>
            mesh.poses.subarray((frame * mesh.count + id) * 6, (frame * mesh.count + id) * 6 + 3),
          );
          const ax = p[0]![0]! / 1.25,
            ay = p[0]![1]! / 1.25,
            bx = p[1]![0]! / 1.25,
            by = p[1]![1]! / 1.25,
            cx = p[2]![0]! / 1.25,
            cy = p[2]![1]! / 1.25;
          const d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
          if (Math.abs(d) < 1e-10) continue;
          const a = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d,
            b = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d,
            c = 1 - a - b;
          if (a < 0 || b < 0 || c < 0) continue;
          const z = a * p[0]![2]! + b * p[1]![2]! + c * p[2]![2]!;
          if (z < depth) {
            depth = z;
            hit = [0, 1].map(
              (axis) =>
                a * mesh.uv[ids[0]! * 2 + axis]! +
                b * mesh.uv[ids[1]! * 2 + axis]! +
                c * mesh.uv[ids[2]! * 2 + axis]!,
            ) as [number, number];
          }
        }
        if (hit) paint.current(...hit);
      };
      const draw = (time: number) => {
        const { direction, animate, phase } = controls.current;
        frame =
          mesh.frames === 1 ? 0 : direction * 32 + (animate ? Math.floor(time / 80) % 32 : phase);
        target.dataset['frame'] = String(frame);
        target.dataset['model'] = asset;
        gl.bindBuffer(gl.ARRAY_BUFFER, poses);
        gl.bufferSubData(
          gl.ARRAY_BUFFER,
          0,
          mesh.poses.subarray(frame * mesh.count * 6, (frame + 1) * mesh.count * 6),
        );
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, colour);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, texture!);
        const { map, revision } = materialMap.current;
        const values = map();
        if (revision !== uploadedMaterials) {
          gl.activeTexture(gl.TEXTURE1);
          gl.bindTexture(gl.TEXTURE_2D, material);
          gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
          gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.R8,
            ATLAS_SIZE,
            ATLAS_SIZE,
            0,
            gl.RED,
            gl.UNSIGNED_BYTE,
            values,
          );
          uploadedMaterials = revision;
        }
        gl.viewport(0, 0, target.width, target.height);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.drawElements(gl.TRIANGLES, mesh.indices.length, gl.UNSIGNED_INT, 0);
        animation = requestAnimationFrame(draw);
      };
      setError('');
      animation = requestAnimationFrame(draw);
    }
    void start().catch((e: unknown) => {
      if (!abort.signal.aborted) setError(e instanceof Error ? e.message : 'Preview unavailable.');
    });
    return () => {
      abort.abort();
      cancelAnimationFrame(animation);
      resources.forEach((dispose) => dispose());
      pick.current = () => {};
      delete target.dataset['frame'];
      delete target.dataset['model'];
    };
  }, [texture, asset, model]);
  return (
    <section
      aria-label="Live colony preview"
      style={{ display: 'flex', flexDirection: 'column', gap: 12, alignItems: 'flex-start' }}
    >
      {ACTIONS[model.id].length > 0 && (
        <label>
          Action{' '}
          <select
            aria-label="Action"
            value={action}
            onChange={(e) => {
              setAction(e.target.value);
              setPhase(0);
            }}
          >
            {ACTIONS[model.id].map((id) => (
              <option key={id} value={id}>
                {id[0]!.toUpperCase() + id.slice(1)}
              </option>
            ))}
          </select>
        </label>
      )}
      <label>
        Direction{' '}
        <input
          type="range"
          min="0"
          max="7"
          disabled={model.id === 'swarm'}
          value={direction}
          onChange={(e) => setDirection(Number(e.target.value))}
        />
      </label>
      <label>
        <input
          type="checkbox"
          disabled={model.id === 'swarm'}
          checked={animate && model.id !== 'swarm'}
          onChange={(e) => setAnimate(e.target.checked)}
        />{' '}
        Animate
      </label>
      <label>
        Frame{' '}
        <input
          type="range"
          min="0"
          max="31"
          aria-label="Frame"
          value={phase}
          disabled={model.id === 'swarm'}
          onChange={(e) => {
            setAnimate(false);
            setPhase(Number(e.target.value));
          }}
        />
        {!animate && <output>{phase + 1} / 32</output>}
      </label>
      <canvas
        ref={canvas}
        width={384}
        height={384}
        aria-label={`Paint directly on the 3D ${model.name.toLowerCase()} model`}
        style={{ width: '100%', maxWidth: 384, touchAction: 'none' }}
        onPointerDown={(e) => {
          stroke.current();
          e.currentTarget.setPointerCapture(e.pointerId);
          const r = e.currentTarget.getBoundingClientRect();
          pick.current(
            ((e.clientX - r.left) / r.width) * 2 - 1,
            1 - ((e.clientY - r.top) / r.height) * 2,
          );
        }}
        onPointerMove={(e) => {
          if (e.buttons) {
            const r = e.currentTarget.getBoundingClientRect();
            pick.current(
              ((e.clientX - r.left) / r.width) * 2 - 1,
              1 - ((e.clientY - r.top) / r.height) * 2,
            );
          }
        }}
      />
      {error && <p role="status">{error}</p>}
    </section>
  );
}
