/* WebGL resources are bounded to the lifetime of this effect. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useMemo, useRef, type ReactNode } from 'react';
import { SKIN_MATERIAL_GLSL } from './materialShader.ts';
import { COLONY_SKIN_SHELLS, type ColonySkinMaterial } from '@glob2/protocol';
import { MATERIAL_GROUPS } from './atlas.ts';
export function StudioIcon({ name }: { name: string }) {
  const paths: Record<string, ReactNode> = {
    brush: (
      <>
        <path d="m14 4 6 6-9 9-6-6Z" />
        <path d="M5 13c-4 0-1 5-4 8 5 0 9-2 7-5M14 4l3-3 6 6-3 3" />
      </>
    ),
    erase: (
      <>
        <path d="m14 3 7 7-10 11H5l-4-5Z" />
        <path d="m7 10 7 7M11 21h11" />
      </>
    ),
    pick: (
      <>
        <path d="m15 3 6 6M18 1l5 5-5 5-5-5ZM14 8 4 18l-1 4 4-1L17 11" />
      </>
    ),
    orbit: (
      <>
        <circle cx="12" cy="12" r="4" />
        <ellipse cx="12" cy="12" rx="11" ry="7" transform="rotate(-30 12 12)" />
      </>
    ),
    patterns: (
      <>
        <path d="m12 2 3 7 7 3-7 3-3 7-3-7-7-3 7-3Z" />
      </>
    ),
    undo: (
      <>
        <path d="m8 4-6 6 6 6M2 10h12a7 7 0 0 1 7 7v3" />
      </>
    ),
    redo: (
      <>
        <path d="m16 4 6 6-6 6m6-6H10a7 7 0 0 0-7 7v3" />
      </>
    ),
    settings: (
      <>
        <circle cx="12" cy="12" r="4" />
        <path d="M12 1v4m0 14v4M1 12h4m14 0h4M4 4l3 3m10 10 3 3M4 20l3-3M17 7l3-3" />
      </>
    ),
    back: <path d="m13 4-8 8 8 8M5 12h17" />,
    close: <path d="m5 5 14 14M5 19 19 5" />,
    play: <path d="m7 3 14 9-14 9Z" />,
    pause: <path d="M8 4v16M16 4v16" />,
    fit: (
      <>
        <path d="M8 2H2v6m14-6h6v6M2 16v6h6m14-6v6h-6" />
        <circle cx="12" cy="12" r="3" />
      </>
    ),
    view: (
      <>
        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8S1 12 1 12Z" />
        <circle cx="12" cy="12" r="3" />
      </>
    ),
    save: (
      <>
        <path d="M3 2h15l4 4v16H3ZM7 2v7h10V2M7 22V13h11v9" />
      </>
    ),
  };
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      {paths[name] ?? paths['view']}
    </svg>
  );
}
export function StudioDialog({
  title,
  onClose,
  children,
  wide = false,
}: {
  title: string;
  onClose: () => void;
  children: ReactNode;
  wide?: boolean;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const d = ref.current;
    const trigger = document.activeElement;
    d?.showModal();
    return () => {
      d?.close();
      if (trigger instanceof HTMLElement && trigger.isConnected) trigger.focus();
    };
  }, []);
  return (
    <dialog
      ref={ref}
      aria-label={title}
      className={`skin-dialog ${wide ? 'skin-dialog-wide' : ''}`}
      onCancel={(e) => {
        e.preventDefault();
        onClose();
      }}
    >
      <header>
        <div>
          <span className="skin-eyebrow">COLONY STUDIO</span>
          <h2>{title}</h2>
        </div>
        <button className="skin-icon-button" aria-label={`Close ${title}`} onClick={onClose}>
          <StudioIcon name="close" />
        </button>
      </header>
      {children}
    </dialog>
  );
}
const SWATCH = 128;
const SWATCH_COLUMNS = 4;
const swatchRows = (count: number) => Math.ceil(count / SWATCH_COLUMNS);
const SWATCH_HEIGHT =
  SWATCH * MATERIAL_GROUPS.reduce((n, g) => n + swatchRows(g.materials.length), 0);
// One analytic sphere per material, lit by the shared material GLSL with fur
// shells drawn over the body as the game does. Every picker shares one hidden
// WebGL context and compiled program, re-drawn per paint colour: browsers drop
// the oldest contexts past a small limit (the studio and the pattern dialog
// each need one for their mesh preview), and the 22-material shader is too
// large to recompile on every colour-slider tick.
type SwatchRenderer = {
  canvas: HTMLCanvasElement;
  gl: WebGL2RenderingContext;
  tint: WebGLUniformLocation | null;
  cell: WebGLUniformLocation | null;
  id: WebGLUniformLocation | null;
  shell: WebGLUniformLocation | null;
};
let swatchRenderer: SwatchRenderer | null | undefined;
function createSwatchRenderer(): SwatchRenderer | null {
  const canvas = document.createElement('canvas');
  canvas.width = SWATCH * SWATCH_COLUMNS;
  canvas.height = SWATCH_HEIGHT;
  const gl = canvas.getContext('webgl2', {
    alpha: true,
    antialias: true,
    preserveDrawingBuffer: true,
  });
  if (!gl) return null;
  // A lost context is recreated on the next render rather than reused.
  canvas.addEventListener('webglcontextlost', () => {
    swatchRenderer = undefined;
  });
  const program = gl.createProgram()!;
  for (const [kind, source] of [
    [gl.VERTEX_SHADER, '#version 300 es\nin vec2 p;void main(){gl_Position=vec4(p,0.,1.);}'],
    [
      gl.FRAGMENT_SHADER,
      `#version 300 es\nprecision highp float;out vec4 color;uniform vec3 tint;uniform vec2 cell;uniform float id;uniform float shell;\n${SKIN_MATERIAL_GLSL}\nvoid main(){vec2 q=(gl_FragCoord.xy-cell-vec2(64.,80.))/(38.*(1.+shell*.2));vec4 shaded=skinShadeSphere(tint,id,q,shell);if(shaded.a<.5)discard;color=vec4(shaded.rgb,1.);}`,
    ],
  ] as const) {
    const s = gl.createShader(kind)!;
    gl.shaderSource(s, source);
    gl.compileShader(s);
    gl.attachShader(program, s);
    gl.deleteShader(s);
  }
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    gl.deleteProgram(program);
    return null;
  }
  gl.useProgram(program);
  gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  const loc = gl.getAttribLocation(program, 'p');
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
  gl.viewport(0, 0, canvas.width, canvas.height);
  gl.clearColor(0, 0, 0, 0);
  gl.enable(gl.SCISSOR_TEST);
  return {
    canvas,
    gl,
    tint: gl.getUniformLocation(program, 'tint'),
    cell: gl.getUniformLocation(program, 'cell'),
    id: gl.getUniformLocation(program, 'id'),
    shell: gl.getUniformLocation(program, 'shell'),
  };
}
/** Draws every group's spheres in `color`; the strips copy their rows out. */
function renderSwatches(color: string): HTMLCanvasElement | null {
  if (swatchRenderer === undefined) swatchRenderer = createSwatchRenderer();
  if (!swatchRenderer) return null;
  const { canvas, gl, tint, cell, id, shell } = swatchRenderer;
  gl.uniform3f(
    tint,
    parseInt(color.slice(1, 3), 16) / 255,
    parseInt(color.slice(3, 5), 16) / 255,
    parseInt(color.slice(5, 7), 16) / 255,
  );
  gl.scissor(0, 0, canvas.width, canvas.height);
  gl.clear(gl.COLOR_BUFFER_BIT);
  let row = 0;
  for (const group of MATERIAL_GROUPS) {
    group.materials.forEach((material, index) => {
      const x = (index % SWATCH_COLUMNS) * SWATCH,
        y = canvas.height - (row + Math.floor(index / SWATCH_COLUMNS) + 1) * SWATCH;
      gl.scissor(x, y, SWATCH, SWATCH);
      gl.uniform2f(cell, x, y);
      gl.uniform1f(id, material.id);
      const passes = material.shells ? COLONY_SKIN_SHELLS : 0;
      for (let k = 0; k <= passes; k++) {
        gl.uniform1f(shell, k / COLONY_SKIN_SHELLS);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
    });
    row += swatchRows(group.materials.length);
  }
  return canvas;
}
function SwatchStrip({
  source,
  color,
  offset,
  materials,
  selected,
  onSelect,
}: {
  source: HTMLCanvasElement | null;
  color: string;
  offset: number;
  materials: readonly ColonySkinMaterial[];
  selected: number;
  onSelect: (id: number) => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const rows = swatchRows(materials.length);
  useEffect(() => {
    const target = canvas.current,
      context = target?.getContext('2d');
    if (!target || !context) return;
    context.clearRect(0, 0, target.width, target.height);
    if (source)
      context.drawImage(
        source,
        0,
        offset,
        target.width,
        target.height,
        0,
        0,
        target.width,
        target.height,
      );
    // `color` keys the copy: the shared source canvas is redrawn in place.
  }, [source, color, offset]);
  return (
    <div className="skin-materials">
      <canvas
        ref={canvas}
        width={SWATCH * SWATCH_COLUMNS}
        height={SWATCH * rows}
        aria-hidden="true"
      />
      <div>
        {materials.map((m) => (
          <button
            key={m.id}
            role="radio"
            aria-checked={selected === m.id}
            onClick={() => onSelect(m.id)}
          >
            <span>{m.name}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
// Row offset of each group in the rendered swatch sheet.
const GROUP_OFFSETS = MATERIAL_GROUPS.reduce<number[]>((offsets, group, index) => {
  offsets.push(
    index
      ? offsets[index - 1]! + SWATCH * swatchRows(MATERIAL_GROUPS[index - 1]!.materials.length)
      : 0,
  );
  return offsets;
}, []);
export function MaterialSwatches({
  color,
  selected,
  onSelect,
}: {
  color: string;
  selected: number;
  onSelect: (id: number) => void;
}) {
  const source = useMemo(() => renderSwatches(color), [color]);
  return (
    <div role="radiogroup" aria-label="Material" className="skin-material-groups">
      {MATERIAL_GROUPS.map((group, index) => (
        <section key={group.name} className="skin-material-group">
          <h4>{group.name}</h4>
          <SwatchStrip
            source={source}
            color={color}
            offset={GROUP_OFFSETS[index]!}
            materials={group.materials}
            selected={selected}
            onSelect={onSelect}
          />
        </section>
      ))}
    </div>
  );
}
