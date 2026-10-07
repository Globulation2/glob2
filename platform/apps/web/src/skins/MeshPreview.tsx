/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import {
  COLONY_SKIN_FUR_LENGTH,
  COLONY_SKIN_SHELLS,
  COLONY_SKIN_SHELL_DEPTH,
  type SwarmMeshId,
} from '@glob2/protocol';
import { type Model, ATLAS_SIZE, regionHasShells } from './atlas.ts';
import { swarmModel } from './swarmShapes.ts';
import {
  furScale,
  loadMesh,
  projectPose,
  type Camera,
  type Mesh,
  type ViewTransform,
} from './geometry.ts';
import { buildProjection, strokeCoverage, padCoverage, type Projection } from './projection.ts';
import { SKIN_MATERIAL_GLSL } from './materialShader.ts';
export type Tool = 'brush' | 'erase' | 'pick' | 'orbit';
export type SceneView = {
  mesh: Mesh;
  view: ViewTransform;
  pose: Float32Array;
  width: number;
  height: number;
  frame: number;
  camera: Camera;
  projection: () => Projection;
};
export type ViewportProps = {
  texture: HTMLCanvasElement;
  materials: Uint8Array;
  revision: number;
  model: Model;
  swarmMesh: SwarmMeshId;
  camera: Camera;
  action: string;
  phase: number;
  animate?: boolean;
  active?: boolean;
  interactive?: boolean;
  tool?: Tool;
  size?: number;
  hardness?: number;
  pressure?: boolean;
  label?: string;
  onCamera?: (camera: Camera) => void;
  onPause?: (frame: number) => void;
  onCoverage?: (coverage: Float32Array, pressure: number) => void;
  onEnd?: (cancel: boolean) => void;
  onPick?: (index: number) => void;
  onScene?: (scene: SceneView) => void;
};
export function MeshPreview(props: ViewportProps) {
  const canvas = useRef<HTMLCanvasElement>(null),
    latest = useRef(props),
    scene = useRef<SceneView | null>(null);
  const invalidate = useRef<() => void>(() => {});
  const [error, setError] = useState(''),
    [attempt, setAttempt] = useState(0),
    [ready, setReady] = useState(false);
  const [cursor, setCursor] = useState<[number, number] | null>(null);
  const pen = useRef<number | null>(null);
  const pointers = useRef(new Map<number, [number, number]>());
  const gesture = useRef<{
    kind: 'paint' | 'orbit';
    pointerId: number;
    last: [number, number];
  } | null>(null);
  const asset =
    props.model.id === 'swarm' ? swarmModel(props.swarmMesh) : `${props.model.id}-${props.action}`;
  useEffect(() => {
    latest.current = props;
    invalidate.current();
  });
  useEffect(() => {
    const target = canvas.current;
    if (!target) return;
    let disposed = false,
      raf = 0,
      uploaded = -1,
      rendered = false,
      stamp = '',
      projectionTimer = 0,
      cachedProjection: Projection | null = null;
    let render: ((time: number) => void) | undefined;
    const resources: (() => void)[] = [];
    const requestDraw = () => {
      if (!disposed && !raf)
        raf = requestAnimationFrame((t) => {
          raf = 0;
          render?.(t);
        });
    };
    invalidate.current = requestDraw;
    const resize = new ResizeObserver(requestDraw);
    resize.observe(target);
    const lost = (event: Event) => {
      event.preventDefault();
      cancelAnimationFrame(raf);
      raf = 0;
      render = undefined;
      clearTimeout(projectionTimer);
      if (gesture.current?.kind === 'paint') latest.current.onEnd?.(true);
      gesture.current = null;
      pointers.current.clear();
      pen.current = null;
      setReady(false);
      setError('The 3D canvas was interrupted. Your painting is safe.');
    };
    const restored = () => setAttempt((n) => n + 1);
    target.addEventListener('webglcontextlost', lost);
    target.addEventListener('webglcontextrestored', restored);
    setReady(false);
    setError('');
    scene.current = null;
    const gl = target.getContext('webgl2', {
      alpha: true,
      antialias: true,
      preserveDrawingBuffer: true,
    });
    async function start() {
      if (!gl)
        throw new Error(
          'Painting needs WebGL 2. You can still save your draft, browse My skins, or visit the shop.',
        );
      const { mesh, view } = await loadMesh(asset);
      if (disposed) return;
      const program = gl.createProgram()!;
      resources.push(() => gl.deleteProgram(program));
      for (const [kind, source] of [
        [
          gl.VERTEX_SHADER,
          // Fur shells push the body outward along the normal and slightly
          // nearer, as the game's tile renderer does.
          `#version 300 es\nin vec3 position;in vec3 normal;in vec2 uv;out vec3 n;out vec2 tex;uniform float shell;uniform vec2 fur;uniform float shellDepth;void main(){gl_Position=vec4(position.xy+normalize(normal).xy*shell*fur,position.z-shell*shellDepth,1.);n=normal;tex=uv;}`,
        ],
        [
          gl.FRAGMENT_SHADER,
          `#version 300 es\nprecision highp float;\n#define SKIN_TEXTURE texture\nin vec3 n;in vec2 tex;out vec4 color;uniform sampler2D paint;uniform sampler2D material;uniform vec2 region;uniform float shell;\n${SKIN_MATERIAL_GLSL}\nvoid main(){vec4 shaded=skinShadeAtlas(paint,material,region,n,tex,shell);if(shaded.a<.5)discard;color=vec4(shaded.rgb,1.);}`,
        ],
      ] as const) {
        const shader = gl.createShader(kind)!;
        resources.push(() => gl.deleteShader(shader));
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
          throw new Error('The 3D shader could not compile.');
        gl.attachShader(program, shader);
      }
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS))
        throw new Error('The 3D canvas could not start.');
      gl.useProgram(program);
      const buffer = (kind: number, values: ArrayBufferView) => {
        const b = gl.createBuffer()!;
        resources.push(() => gl.deleteBuffer(b));
        gl.bindBuffer(kind, b);
        gl.bufferData(kind, values, gl.DYNAMIC_DRAW);
        return b;
      };
      const positions = buffer(gl.ARRAY_BUFFER, mesh.poses.subarray(0, mesh.count * 6));
      for (const [name, offset] of [
        ['position', 0],
        ['normal', 12],
      ] as const) {
        const loc = gl.getAttribLocation(program, name);
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 24, offset);
      }
      buffer(gl.ARRAY_BUFFER, mesh.uv);
      const loc = gl.getAttribLocation(program, 'uv');
      gl.enableVertexAttribArray(loc);
      gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
      buffer(gl.ELEMENT_ARRAY_BUFFER, mesh.indices);
      const textures = [0, 1].map((unit) => {
        const t = gl.createTexture()!;
        resources.push(() => gl.deleteTexture(t));
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, unit ? gl.NEAREST : gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, unit ? gl.NEAREST : gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return t;
      });
      gl.uniform1i(gl.getUniformLocation(program, 'paint'), 0);
      gl.uniform1i(gl.getUniformLocation(program, 'material'), 1);
      gl.uniform2f(
        gl.getUniformLocation(program, 'region'),
        latest.current.model.x / ATLAS_SIZE,
        latest.current.model.y / ATLAS_SIZE,
      );
      const shellLocation = gl.getUniformLocation(program, 'shell'),
        furLocation = gl.getUniformLocation(program, 'fur');
      gl.uniform1f(gl.getUniformLocation(program, 'shellDepth'), COLONY_SKIN_SHELL_DEPTH);
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.clearColor(0, 0, 0, 0);
      let animationStart = 0,
        playing = false,
        startFrame = 0,
        shells = 0;
      render = (time) => {
        const p = latest.current;
        if (p.active === false) return;
        const width = target!.clientWidth,
          height = target!.clientHeight;
        if (!width || !height) return;
        const dpr = Math.min(2, window.devicePixelRatio || 1);
        const rw = Math.round(width * dpr),
          rh = Math.round(height * dpr);
        if (target!.width !== rw || target!.height !== rh) {
          target!.width = rw;
          target!.height = rh;
        }
        if (p.animate && !playing) {
          animationStart = time;
          startFrame = p.phase;
        }
        playing = !!p.animate;
        const frame =
          mesh.frames === 1
            ? 0
            : playing
              ? (startFrame + Math.floor((time - animationStart) / 80)) % 32
              : p.phase;
        const nextStamp = JSON.stringify([frame, p.camera, width, height]);
        if (nextStamp !== stamp) {
          stamp = nextStamp;
          cachedProjection = null;
          const pose = projectPose(mesh, view, frame, p.camera, width / height);
          scene.current = {
            mesh,
            view,
            pose,
            width,
            height,
            frame,
            camera: { ...p.camera },
            projection: () => (cachedProjection ??= buildProjection(mesh, pose, width, height)),
          };
          clearTimeout(projectionTimer);
          // Prepare the stationary view between gestures so the first brush dab
          // does not pay for visibility rasterization. Orbit/animation cancels it.
          if (p.onCoverage && !playing && !p.camera.game) {
            projectionTimer = window.setTimeout(() => {
              if (!disposed && latest.current.active !== false && scene.current?.pose === pose)
                scene.current.projection();
            }, 80);
          }
          gl.bindBuffer(gl.ARRAY_BUFFER, positions);
          gl.bufferSubData(gl.ARRAY_BUFFER, 0, pose);
          const fur = furScale(mesh, view, p.camera, width / height);
          gl.uniform2f(
            furLocation,
            fur[0] * COLONY_SKIN_FUR_LENGTH,
            fur[1] * COLONY_SKIN_FUR_LENGTH,
          );
          p.onScene?.(scene.current);
        }
        target!.dataset['model'] = asset;
        target!.dataset['frame'] = String(frame);
        if (uploaded !== p.revision) {
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, textures[0]!);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, p.texture);
          gl.activeTexture(gl.TEXTURE1);
          gl.bindTexture(gl.TEXTURE_2D, textures[1]!);
          gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
          gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.R8,
            512,
            512,
            0,
            gl.RED,
            gl.UNSIGNED_BYTE,
            p.materials,
          );
          shells = regionHasShells(p.materials, p.model) ? COLONY_SKIN_SHELLS : 0;
          uploaded = p.revision;
        }
        gl.viewport(0, 0, rw, rh);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        for (let shell = 0; shell <= shells; shell++) {
          gl.uniform1f(shellLocation, shell / COLONY_SKIN_SHELLS);
          gl.drawElements(gl.TRIANGLES, mesh.indices.length, gl.UNSIGNED_INT, 0);
        }
        if (!rendered) {
          rendered = true;
          setReady(true);
        }
        if (playing) requestDraw();
      };
      requestDraw();
    }
    void start().catch((e: unknown) => {
      if (!disposed) setError(e instanceof Error ? e.message : 'Preview unavailable.');
    });
    return () => {
      disposed = true;
      cancelAnimationFrame(raf);
      clearTimeout(projectionTimer);
      resize.disconnect();
      target.removeEventListener('webglcontextlost', lost);
      target.removeEventListener('webglcontextrestored', restored);
      resources.forEach((f) => f());
      scene.current = null;
      invalidate.current = () => {};
    };
  }, [asset, attempt]);
  useEffect(() => {
    const target = canvas.current;
    if (!target) return;
    const wheel = (event: WheelEvent) => {
      const current = latest.current;
      if (current.active === false || current.interactive === false) return;
      // Trackpad pinch arrives as Ctrl+wheel. Consume it so the browser does
      // not zoom the page as well; final-view framing remains fixed.
      event.preventDefault();
      if (current.camera.game) return;
      current.onCamera?.({
        ...current.camera,
        zoom: Math.max(0.45, Math.min(4, current.camera.zoom * Math.exp(-event.deltaY * 0.001))),
      });
    };
    target.addEventListener('wheel', wheel, { passive: false });
    return () => target.removeEventListener('wheel', wheel);
  }, []);
  function at(e: React.PointerEvent<HTMLCanvasElement>): [number, number] {
    const r = e.currentTarget.getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top];
  }
  function dab(from: [number, number], to: [number, number], pressure: number) {
    if (!scene.current) return;
    const p = scene.current.projection();
    const radius = ((props.size ?? 28) / 2) * (props.pressure ? Math.max(0.2, pressure) : 1);
    const coverage = strokeCoverage(p, from, to, radius, props.hardness ?? 0.8);
    if (coverage.some((v) => v > 0))
      props.onCoverage?.(
        padCoverage(coverage, p.used),
        props.pressure ? Math.max(0.1, pressure) : 1,
      );
  }
  function end(cancel: boolean) {
    if (gesture.current?.kind === 'paint') props.onEnd?.(cancel);
    gesture.current = null;
  }
  function releasePointer(pointerId: number, cancel: boolean) {
    if (!pointers.current.delete(pointerId)) return;
    if (pen.current === pointerId) pen.current = null;
    if (gesture.current?.pointerId === pointerId) end(cancel);
  }
  useEffect(() => {
    if (props.active === false || props.interactive === false) {
      if (gesture.current?.kind === 'paint') latest.current.onEnd?.(true);
      gesture.current = null;
      pointers.current.clear();
      pen.current = null;
    }
  }, [props.active, props.interactive]);
  return (
    <div className={`skin-viewport ${props.interactive === false ? 'is-reference' : ''}`}>
      <canvas
        ref={canvas}
        aria-label={
          props.label ?? `Paint directly on the 3D ${props.model.name.toLowerCase()} model`
        }
        onContextMenu={(e) => e.preventDefault()}
        onPointerDown={(e) => {
          if (!ready || props.active === false || props.interactive === false || !scene.current)
            return;
          // Only the primary button paints; the secondary button explicitly orbits.
          if (e.button !== 0 && e.button !== 2) return;
          if (e.pointerType === 'touch' && pen.current !== null) return;
          if (e.pointerType === 'pen') {
            // A palm can touch first. Discard that provisional touch stroke when
            // the pen takes over, and ignore its later capture-loss event.
            end(true);
            pointers.current.clear();
            pen.current = e.pointerId;
          }
          e.currentTarget.setPointerCapture(e.pointerId);
          const pos = at(e);
          pointers.current.set(e.pointerId, pos);
          if (pointers.current.size > 1) {
            end(true);
            return;
          }
          const orbit = props.tool === 'orbit' || e.button === 2 || e.altKey || props.camera.game;
          gesture.current = {
            kind: orbit ? 'orbit' : 'paint',
            pointerId: e.pointerId,
            last: pos,
          };
          if (!orbit) {
            props.onPause?.(scene.current.frame);
            if (props.tool === 'pick') {
              const p = scene.current.projection();
              let nearest = -1,
                distance = 144;
              for (let i = 0; i < 65536; i++)
                if (p.visible[i]) {
                  const d = (p.x[i]! - pos[0]) ** 2 + (p.y[i]! - pos[1]) ** 2;
                  if (d < distance) {
                    nearest = i;
                    distance = d;
                  }
                }
              if (nearest >= 0) props.onPick?.(nearest);
              gesture.current = null;
            } else dab(pos, pos, e.pointerType === 'pen' ? e.pressure : 1);
          }
        }}
        onPointerMove={(e) => {
          const pos = at(e);
          if (e.pointerType !== 'touch') setCursor(pos);
          if (!pointers.current.has(e.pointerId)) return;
          const old = pointers.current.get(e.pointerId)!;
          if (pointers.current.size > 1) {
            const other = [...pointers.current.entries()].find(([id]) => id !== e.pointerId)![1];
            const before = Math.hypot(old[0] - other[0], old[1] - other[1]),
              after = Math.hypot(pos[0] - other[0], pos[1] - other[1]);
            if (!props.camera.game && before > 1)
              props.onCamera?.({
                ...props.camera,
                zoom: Math.max(0.45, Math.min(4, (props.camera.zoom * after) / before)),
              });
          } else if (gesture.current) {
            const previous = gesture.current.last;
            if (gesture.current.kind === 'orbit') {
              const dx = pos[0] - previous[0];
              props.onCamera?.(
                props.camera.game
                  ? {
                      ...props.camera,
                      angle: ((Math.round(props.camera.angle - dx) % 360) + 360) % 360,
                    }
                  : {
                      ...props.camera,
                      yaw: props.camera.yaw - dx * 0.008,
                    },
              );
            } else dab(previous, pos, e.pointerType === 'pen' ? e.pressure : 1);
            gesture.current.last = pos;
          }
          pointers.current.set(e.pointerId, pos);
        }}
        onPointerUp={(e) => releasePointer(e.pointerId, false)}
        onPointerCancel={(e) => releasePointer(e.pointerId, true)}
        onLostPointerCapture={(e) => releasePointer(e.pointerId, true)}
        onPointerLeave={() => setCursor(null)}
      />
      {props.interactive !== false &&
        cursor &&
        (props.tool === 'brush' || props.tool === 'erase') &&
        !props.camera.game && (
          <span
            className="skin-brush-cursor"
            style={{ left: cursor[0], top: cursor[1], width: props.size, height: props.size }}
          />
        )}
      {!ready && !error && (
        <div className="skin-canvas-message" role="status">
          Preparing your model…
        </div>
      )}
      {error && (
        <div className="skin-canvas-message" role="alert">
          <p>{props.interactive === false ? 'Preview unavailable' : error}</p>
          {props.interactive !== false && (
            <button onClick={() => setAttempt((n) => n + 1)}>Retry 3D canvas</button>
          )}
        </div>
      )}
    </div>
  );
}
