/* WebGL resources are bounded to the lifetime of this effect. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, type ReactNode } from 'react';
import { SKIN_MATERIAL_GLSL } from './materialShader.ts';
import { MATERIALS } from './atlas.ts';
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
export function MaterialSwatches({
  color,
  selected,
  onSelect,
}: {
  color: string;
  selected: number;
  onSelect: (id: number) => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const target = canvas.current,
      gl = target?.getContext('webgl2', { alpha: true, antialias: true });
    if (!gl || !target) return;
    const program = gl.createProgram()!;
    const shaders: WebGLShader[] = [];
    for (const [kind, source] of [
      [gl.VERTEX_SHADER, '#version 300 es\nin vec2 p;void main(){gl_Position=vec4(p,0.,1.);}'],
      [
        gl.FRAGMENT_SHADER,
        `#version 300 es\nprecision highp float;out vec4 color;uniform vec3 tint;\n${SKIN_MATERIAL_GLSL}\nvoid main(){float id=floor(gl_FragCoord.x/128.);vec2 q=vec2(mod(gl_FragCoord.x,128.),gl_FragCoord.y)/64.-1.;q*=1.3;float r=dot(q,q);if(r>1.)discard;vec3 n=vec3(q,sqrt(1.-r));color=vec4(skinShade(tint,id,n,(q+1.)*.5),1.);}`,
      ],
    ] as const) {
      const s = gl.createShader(kind)!;
      gl.shaderSource(s, source);
      gl.compileShader(s);
      gl.attachShader(program, s);
      shaders.push(s);
    }
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      shaders.forEach((s) => gl.deleteShader(s));
      gl.deleteProgram(program);
      return;
    }
    gl.useProgram(program);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(program, 'p');
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.uniform3f(
      gl.getUniformLocation(program, 'tint'),
      parseInt(color.slice(1, 3), 16) / 255,
      parseInt(color.slice(3, 5), 16) / 255,
      parseInt(color.slice(5, 7), 16) / 255,
    );
    gl.viewport(0, 0, 512, 128);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    return () => {
      gl.deleteBuffer(buffer);
      shaders.forEach((s) => gl.deleteShader(s));
      gl.deleteProgram(program);
    };
  }, [color]);
  return (
    <div className="skin-materials">
      <canvas ref={canvas} width={512} height={128} aria-hidden="true" />
      <div role="radiogroup" aria-label="Material">
        {MATERIALS.map((m) => (
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
