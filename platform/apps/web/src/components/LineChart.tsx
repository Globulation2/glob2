// A small SVG line chart: one y axis, 2px lines, recessive grid, a legend for
// two or more series, and a crosshair tooltip on hover/touch. A visually
// hidden table carries the same numbers for screen readers.
import { useEffect, useId, useMemo, useRef, useState, type PointerEvent } from 'react';

export interface Series {
  name: string;
  color: string;
  points: { x: number; y: number }[];
  dashed?: boolean;
}

interface Props {
  series: Series[];
  title: string;
  height?: number;
  xFormat?: (x: number) => string;
  yFormat?: (y: number) => string;
  /** Include zero on the y axis (counts) or fit the data (ratings). */
  zeroBased?: boolean;
  /** Mark each point (sparse series such as ratings). */
  dots?: boolean;
}

const PAD = { left: 44, right: 12, top: 10, bottom: 24 };

function niceTicks(min: number, max: number, count: number): number[] {
  if (min === max) return [min];
  const raw = (max - min) / count;
  const magnitude = 10 ** Math.floor(Math.log10(raw));
  const step = ([1, 2, 2.5, 5, 10].find((m) => m * magnitude >= raw) ?? 10) * magnitude;
  const ticks = [];
  for (let v = Math.ceil(min / step) * step; v <= max + step / 1e6; v += step) {
    ticks.push(Math.round(v / step) * step);
  }
  return ticks;
}

export function LineChart({
  series,
  title,
  height = 220,
  xFormat = String,
  yFormat = (y) => String(Math.round(y)),
  zeroBased = true,
  dots = false,
}: Props) {
  const id = useId();
  const box = useRef<HTMLElement>(null);
  // Draw at the container's pixel width so text and strokes keep their size.
  const [WIDTH, setWidth] = useState(640);
  useEffect(() => {
    const element = box.current;
    if (!element || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(([entry]) => {
      const width = Math.round(entry?.contentRect.width ?? 0);
      if (width > 0) setWidth(width);
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  const [hover, setHover] = useState<
    { x: number; left: number; top: number; width: number } | undefined
  >();

  const geometry = useMemo(() => {
    const xs = series.flatMap((s) => s.points.map((p) => p.x));
    const ys = series.flatMap((s) => s.points.map((p) => p.y));
    if (xs.length === 0) return undefined;
    const xMin = Math.min(...xs);
    const xMax = Math.max(...xs);
    let yMin = zeroBased ? Math.min(0, ...ys) : Math.min(...ys);
    let yMax = Math.max(...ys);
    if (yMax === yMin) {
      yMax += 1;
      yMin -= zeroBased ? 0 : 1;
    }
    const span = yMax - yMin;
    if (!zeroBased) {
      yMin -= span * 0.08;
      yMax += span * 0.08;
    } else {
      yMax += span * 0.05;
    }
    const yTicks = niceTicks(yMin, yMax, 4);
    const xTicks = niceTicks(xMin, xMax, Math.max(2, Math.floor(WIDTH / 120)));
    const sx = (x: number) =>
      PAD.left +
      (xMax === xMin ? 0.5 : (x - xMin) / (xMax - xMin)) * (WIDTH - PAD.left - PAD.right);
    const sy = (y: number) =>
      PAD.top + (1 - (y - yMin) / (yMax - yMin)) * (height - PAD.top - PAD.bottom);
    const allX = [...new Set(xs)].sort((a, b) => a - b);
    return { sx, sy, yTicks, xTicks, allX, xMin, xMax };
  }, [series, height, zeroBased, WIDTH]);

  if (!geometry) return <div className="chart empty">No data yet.</div>;
  const { sx, sy, yTicks, xTicks, allX } = geometry;

  const onMove = (event: PointerEvent<SVGSVGElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const scale = WIDTH / rect.width;
    const px = (event.clientX - rect.left) * scale;
    let best = allX[0] ?? 0;
    for (const x of allX) if (Math.abs(sx(x) - px) < Math.abs(sx(best) - px)) best = x;
    const outer = box.current?.getBoundingClientRect();
    setHover({
      width: outer?.width ?? rect.width,
      x: best,
      left: sx(best) / scale + (rect.left - (outer?.left ?? rect.left)),
      top: event.clientY - (outer?.top ?? rect.top),
    });
  };

  const hovered = hover
    ? series.flatMap((s) => {
        const point = s.points.find((p) => p.x === hover.x);
        return point ? [{ s, point }] : [];
      })
    : [];

  return (
    <figure className="chart" ref={box} aria-labelledby={`${id}-title`} style={{ margin: 0 }}>
      <figcaption id={`${id}-title`} className="caption" style={{ margin: '0 2px 4px' }}>
        {title}
      </figcaption>
      <svg
        viewBox={`0 0 ${WIDTH} ${height}`}
        role="img"
        aria-label={title}
        onPointerMove={onMove}
        onPointerDown={onMove}
        onPointerLeave={() => setHover(undefined)}
        style={{ touchAction: 'pan-y' }}
      >
        <g className="axis">
          {yTicks.map((y) => (
            <g key={`y${y}`}>
              <line
                className="gridline"
                x1={PAD.left}
                x2={WIDTH - PAD.right}
                y1={sy(y)}
                y2={sy(y)}
              />
              <text x={PAD.left - 6} y={sy(y) + 4} textAnchor="end">
                {yFormat(y)}
              </text>
            </g>
          ))}
          {xTicks.map((x) => (
            <text key={`x${x}`} x={sx(x)} y={height - 6} textAnchor="middle">
              {xFormat(x)}
            </text>
          ))}
        </g>
        {series.map((s) => (
          <g key={s.name}>
            <polyline
              fill="none"
              stroke={s.color}
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
              strokeDasharray={s.dashed ? '5 4' : undefined}
              points={s.points.map((p) => `${sx(p.x)},${sy(p.y)}`).join(' ')}
            />
            {dots &&
              s.points.map((p) => (
                <circle
                  key={p.x}
                  cx={sx(p.x)}
                  cy={sy(p.y)}
                  r={4}
                  fill={s.color}
                  stroke="var(--field)"
                  strokeWidth={2}
                />
              ))}
          </g>
        ))}
        {hover && (
          <g pointerEvents="none">
            <line
              x1={sx(hover.x)}
              x2={sx(hover.x)}
              y1={PAD.top}
              y2={height - PAD.bottom}
              stroke="var(--muted)"
              strokeWidth={1}
            />
            {hovered.map(({ s, point }) => (
              <circle
                key={s.name}
                cx={sx(point.x)}
                cy={sy(point.y)}
                r={5}
                fill={s.color}
                stroke="var(--field)"
                strokeWidth={2}
              />
            ))}
          </g>
        )}
      </svg>
      {hover && hovered.length > 0 && (
        <div
          className="tip"
          role="status"
          style={{
            left: Math.min(hover.left + 12, hover.width - 170),
            top: Math.max(0, hover.top - 50),
          }}
        >
          <div>{xFormat(hover.x)}</div>
          {hovered.map(({ s, point }) => (
            <div key={s.name}>
              <span className="sw" style={{ background: s.color }} /> {s.name}: {yFormat(point.y)}
            </div>
          ))}
        </div>
      )}
      {series.length > 1 && (
        <div className="legend">
          {series.map((s) => (
            <span className="key" key={s.name}>
              <span className={`line${s.dashed ? ' dash' : ''}`} style={{ color: s.color }} />
              {s.name}
            </span>
          ))}
        </div>
      )}
      <table className="sr-only">
        <caption>{title}</caption>
        <thead>
          <tr>
            <th>x</th>
            {series.map((s) => (
              <th key={s.name}>{s.name}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {allX.map((x) => (
            <tr key={x}>
              <td>{xFormat(x)}</td>
              {series.map((s) => {
                const point = s.points.find((p) => p.x === x);
                return <td key={s.name}>{point ? yFormat(point.y) : ''}</td>;
              })}
            </tr>
          ))}
        </tbody>
      </table>
    </figure>
  );
}
