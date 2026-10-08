import { useId, useState, type PointerEvent } from 'react';

type HSV = { h: number; s: number; v: number };
const SWATCHES = [
  '#ed9252',
  '#f4cf65',
  '#92d449',
  '#39bca2',
  '#4aa6df',
  '#8e52cc',
  '#e86b9c',
  '#ffffff',
  '#808080',
  '#16121c',
];
const clamp = (n: number) => Math.max(0, Math.min(1, n));
function fromHex(hex: string): HSV {
  const channel = (i: number) => parseInt(hex.slice(i, i + 2), 16) / 255;
  const r = channel(1),
    g = channel(3),
    b = channel(5);
  const v = Math.max(r, g, b),
    delta = v - Math.min(r, g, b);
  const h = !delta
    ? 0
    : v === r
      ? (g - b) / delta
      : v === g
        ? (b - r) / delta + 2
        : (r - g) / delta + 4;
  return { h: (h * 60 + 360) % 360, s: v ? delta / v : 0, v };
}
function toHex({ h, s, v }: HSV) {
  const c = v * s,
    x = c * (1 - Math.abs(((h / 60) % 2) - 1)),
    m = v - c;
  const rgb =
    h < 60
      ? [c, x, 0]
      : h < 120
        ? [x, c, 0]
        : h < 180
          ? [0, c, x]
          : h < 240
            ? [0, x, c]
            : h < 300
              ? [x, 0, c]
              : [c, 0, x];
  return (
    '#' +
    rgb
      .map((n) =>
        Math.round((n + m) * 255)
          .toString(16)
          .padStart(2, '0'),
      )
      .join('')
  );
}

/** Stays inside its editor panel, including on browsers with an OS color dialog. */
export function ColorPicker({
  label,
  value,
  onChange,
  description,
}: {
  label: string;
  value: string;
  onChange: (color: string) => void;
  description?: string;
}) {
  const id = useId();
  const [current, setCurrent] = useState(value);
  const [hsv, setHSV] = useState(() => fromHex(value));
  const [hex, setHex] = useState(value);
  // Synchronize eyedropper, undo and document changes; keep hue when our own
  // selection becomes black or grey so the hue slider remains usable.
  if (current !== value) {
    setCurrent(value);
    setHSV(fromHex(value));
    setHex(value);
  }
  function select(next: HSV) {
    const color = toHex(next);
    setHSV(next);
    setCurrent(color);
    setHex(color);
    onChange(color);
  }
  function point(event: PointerEvent<HTMLDivElement>) {
    const bounds = event.currentTarget.getBoundingClientRect();
    if (!bounds.width || !bounds.height) return;
    select({
      ...hsv,
      s: clamp((event.clientX - bounds.left) / bounds.width),
      v: 1 - clamp((event.clientY - bounds.top) / bounds.height),
    });
  }
  return (
    <details className="skin-color-picker">
      <summary aria-label={`Choose ${label.toLowerCase()}`}>
        <span className="skin-color-chip" style={{ background: value }} />
        <span className="skin-color-caption">
          <strong>{label}</strong>
          <small>{value.toUpperCase()}</small>
        </span>
        <span aria-hidden="true" className="skin-color-chevron">
          ⌄
        </span>
      </summary>
      {description && <p className="skin-color-description">{description}</p>}
      <div className="skin-color-controls">
        <div
          className="skin-color-plane"
          role="slider"
          tabIndex={0}
          aria-label={`${label} saturation and brightness`}
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={Math.round(hsv.s * 100)}
          aria-valuetext={`${Math.round(hsv.s * 100)}% saturation, ${Math.round(hsv.v * 100)}% brightness`}
          aria-describedby={id}
          style={{ backgroundColor: `hsl(${hsv.h}, 100%, 50%)` }}
          onPointerDown={(event) => {
            if (event.button !== 0) return;
            event.currentTarget.focus();
            event.currentTarget.setPointerCapture(event.pointerId);
            point(event);
          }}
          onPointerMove={(event) => {
            if (event.currentTarget.hasPointerCapture(event.pointerId)) point(event);
          }}
          onPointerUp={(event) => {
            if (event.currentTarget.hasPointerCapture(event.pointerId))
              event.currentTarget.releasePointerCapture(event.pointerId);
          }}
          onKeyDown={(event) => {
            const step = event.shiftKey ? 0.1 : 0.01;
            if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) return;
            event.preventDefault();
            event.stopPropagation();
            select({
              ...hsv,
              s: clamp(
                hsv.s + (event.key === 'ArrowRight' ? step : event.key === 'ArrowLeft' ? -step : 0),
              ),
              v: clamp(
                hsv.v + (event.key === 'ArrowUp' ? step : event.key === 'ArrowDown' ? -step : 0),
              ),
            });
          }}
        >
          <span
            className="skin-color-cursor"
            style={{ left: `${hsv.s * 100}%`, top: `${(1 - hsv.v) * 100}%`, background: value }}
          />
        </div>
        <span id={id} className="skin-color-help">
          ← → saturation · ↑ ↓ brightness
        </span>
        <label className="skin-color-hue">
          Hue
          <input
            type="range"
            aria-label={`${label} hue`}
            min={0}
            max={359}
            value={Math.round(hsv.h)}
            onChange={(event) => select({ ...hsv, h: Number(event.target.value) })}
          />
        </label>
        <label className="skin-color-hex">
          Hex
          <input
            aria-label={label}
            type="text"
            value={hex}
            spellCheck={false}
            autoComplete="off"
            aria-invalid={!/^#?[\da-f]{6}$/i.test(hex)}
            onChange={(event) => {
              const text = event.target.value;
              setHex(text);
              if (/^#?[\da-f]{6}$/i.test(text)) {
                const color = '#' + text.replace('#', '').toLowerCase();
                setCurrent(color);
                setHSV(fromHex(color));
                onChange(color);
              }
            }}
            onBlur={() => setHex(value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter' || event.key === 'Escape') {
                event.preventDefault();
                event.stopPropagation();
                setHex(value);
                event.currentTarget.blur();
              }
            }}
          />
        </label>
        <div className="skin-color-swatches" role="group" aria-label={`${label} swatches`}>
          {SWATCHES.map((color) => (
            <button
              key={color}
              type="button"
              aria-label={`${label} ${color.toUpperCase()}`}
              aria-pressed={value.toLowerCase() === color}
              style={{ background: color }}
              onClick={() => select(fromHex(color))}
            />
          ))}
        </div>
      </div>
    </details>
  );
}
