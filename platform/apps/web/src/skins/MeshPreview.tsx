/* Indexed geometry is bounded by decode before rendering or hit testing. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import type { SwarmMeshId } from '@glob2/protocol';
import { swarmModel } from './swarmShapes.ts';

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
const models = {
  worker: { label: 'Worker', actions: ['walk', 'swim', 'harvest'] },
  warrior: { label: 'Warrior', actions: ['walk', 'swim', 'fight'] },
  explorer: { label: 'Explorer', actions: ['fly'] },
  swarm: { label: 'Swarm', actions: [] },
} as const;
type Model = keyof typeof models;

export function MeshPreview({
  texture,
  swarmMesh,
  onPaint,
  onStroke,
}: {
  texture: HTMLCanvasElement | null;
  swarmMesh: SwarmMeshId;
  onPaint: (u: number, v: number) => void;
  onStroke: () => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const pick = useRef<(x: number, y: number) => void>(() => {});
  const paint = useRef(onPaint),
    stroke = useRef(onStroke);
  useEffect(() => {
    paint.current = onPaint;
    stroke.current = onStroke;
  });
  const [model, setModel] = useState<Model>('worker');
  const [action, setAction] = useState('walk');
  // Choosing a swarm shape shows it; the swarm entry always previews the chosen shape.
  const [shownMesh, setShownMesh] = useState(swarmMesh);
  if (shownMesh !== swarmMesh) {
    setShownMesh(swarmMesh);
    setModel('swarm');
    setAction('');
  }
  const [phase, setPhase] = useState(0);
  const [direction, setDirection] = useState(0);
  const [animate, setAnimate] = useState(true);
  const [error, setError] = useState('');
  const controls = useRef({ direction, animate, phase });
  useEffect(() => {
    controls.current = { direction, animate, phase };
  }, [direction, animate, phase]);
  const asset = model === 'swarm' ? swarmModel(swarmMesh) : `${model}-${action}`;
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
        precision mediump float; in vec3 n; in vec2 tex; uniform sampler2D paint; out vec4 color;
        void main(){float light=.45+.55*max(0.,dot(normalize(n),normalize(vec3(-.4,.7,1.))));color=vec4(texture(paint,tex).rgb*light,1.);}`,
        ),
      );
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS))
        throw new Error('3D preview could not start.');
      gl.useProgram(program);
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
      const tex = gl.createTexture()!;
      resources.push(() => gl.deleteTexture(tex));
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
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
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, texture!);
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
  }, [texture, asset]);
  return (
    <section
      aria-label="Live colony preview"
      style={{ display: 'flex', flexDirection: 'column', gap: 12, alignItems: 'flex-start' }}
    >
      <label>
        Preview model{' '}
        <select
          aria-label="Preview model"
          value={model}
          onChange={(e) => {
            const next = e.target.value as Model;
            setModel(next);
            setAction(models[next].actions[0] ?? '');
            setPhase(0);
          }}
        >
          {Object.entries(models).map(([id, item]) => (
            <option key={id} value={id}>
              {item.label}
            </option>
          ))}
        </select>
      </label>
      {models[model].actions.length > 0 && (
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
            {models[model].actions.map((id) => (
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
          disabled={model === 'swarm'}
          value={direction}
          onChange={(e) => setDirection(Number(e.target.value))}
        />
      </label>
      <label>
        <input
          type="checkbox"
          disabled={model === 'swarm'}
          checked={animate && model !== 'swarm'}
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
          disabled={model === 'swarm'}
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
        aria-label="Paint directly on the 3D colony model"
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
