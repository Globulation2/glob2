import { t, useLocale } from '../i18n.tsx';
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
  /** Whole-number y values (counts, ratings): ticks on whole steps only. */
  integer?: boolean;
  /** Fixed reporting range, so sparse observations do not shrink the time axis. */
  xDomain?: readonly [number, number];
  /** Split lines across missing observations instead of implying continuous data. */
  maxGap?: number;
  xLabel?: string;
  yLabel?: string;
}

export function splitPoints(points: Series['points'], maxGap = Infinity) {
  const segments: Series['points'][] = [];
  for (const point of points) {
    const segment = segments.at(-1);
    const previous = segment?.at(-1);
    if (!segment || (previous && point.x - previous.x > maxGap)) segments.push([point]);
    else segment.push(point);
  }
  return segments;
}

const PAD = { left: 44, right: 12, top: 10, bottom: 24 };

/**
 * Round tick values. Integer axes (counts, ratings) only use whole steps from
 * 1, 2, 5, 10, so a small range never shows rounded duplicates ("0, 1, 1")
 * or uneven labels ("0, 3, 5, 8").
 */
export function niceTicks(min: number, max: number, count: number, integer = false): number[] {
  if (min === max) return [min];
  const raw = (max - min) / count;
  const magnitude = 10 ** Math.floor(Math.log10(raw));
  const steps = integer ? [1, 2, 5, 10] : [1, 2, 2.5, 5, 10];
  // Whole steps are coarser, so they may run a little under the target spacing.
  const want = integer ? raw * 0.75 : raw;
  let step = (steps.find((m) => m * magnitude >= want) ?? 10) * magnitude;
  if (integer) step = Math.max(1, Math.round(step));
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
  integer = true,
  xDomain,
  maxGap,
  xLabel = 'x',
  yLabel,
}: Props) {
  useLocale();
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
    const xMin = xDomain?.[0] ?? Math.min(...xs);
    const xMax = xDomain?.[1] ?? Math.max(...xs);
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
    const yTicks = niceTicks(yMin, yMax, 4, integer);
    const tickCount = Math.max(2, Math.floor(WIDTH / 120));
    // Fixed ranges always label their boundaries; epoch-based nice ticks can
    // leave a phone chart with one arbitrary date and no visible range.
    const xTicks = xDomain
      ? Array.from({ length: tickCount }, (_, i) => xMin + ((xMax - xMin) * i) / (tickCount - 1))
      : niceTicks(xMin, xMax, tickCount);
    const sx = (x: number) =>
      PAD.left +
      (xMax === xMin ? 0.5 : (x - xMin) / (xMax - xMin)) * (WIDTH - PAD.left - PAD.right);
    const sy = (y: number) =>
      PAD.top + (1 - (y - yMin) / (yMax - yMin)) * (height - PAD.top - PAD.bottom);
    const allX = [...new Set(xs)].sort((a, b) => a - b);
    return { sx, sy, yTicks, xTicks, allX, xMin, xMax };
  }, [series, height, zeroBased, integer, WIDTH, xDomain]);

  if (!geometry) return <div className="chart empty">{t('No data yet.')}</div>;
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
      <figcaption id={`${id}-title`} style={{ margin: '0 2px 6px' }}>
        {title}
        {yLabel ? ` · ${yLabel}` : ''}
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
          <line
            className="baseline"
            x1={PAD.left}
            x2={WIDTH - PAD.right}
            y1={height - PAD.bottom}
            y2={height - PAD.bottom}
          />
          {xTicks.map((x, index) => (
            <text
              key={`x${x}`}
              x={sx(x)}
              y={height - 6}
              textAnchor={
                xDomain && index === 0
                  ? 'start'
                  : xDomain && index === xTicks.length - 1
                    ? 'end'
                    : 'middle'
              }
            >
              {xFormat(x)}
            </text>
          ))}
        </g>
        {series.map((s) => (
          <g key={s.name}>
            {splitPoints(s.points, maxGap).map((segment, index) => (
              <polyline
                key={index}
                fill="none"
                stroke={s.color}
                strokeWidth={2.5}
                strokeLinejoin="round"
                strokeLinecap="round"
                strokeDasharray={s.dashed ? '5 4' : undefined}
                points={segment.map((p) => `${sx(p.x)},${sy(p.y)}`).join(' ')}
              />
            ))}
            {dots &&
              s.points.map((p) => (
                <circle
                  key={p.x}
                  cx={sx(p.x)}
                  cy={sy(p.y)}
                  r={4}
                  fill={s.color}
                  stroke="var(--surface)"
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
              stroke="var(--ink-2)"
              strokeDasharray="3 3"
              strokeWidth={1}
            />
            {hovered.map(({ s, point }) => (
              <circle
                key={s.name}
                cx={sx(point.x)}
                cy={sy(point.y)}
                r={5}
                fill={s.color}
                stroke="var(--surface)"
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
              <span className="sw" style={{ background: s.color }} /> {s.name}
              {t(': ')}
              {yFormat(point.y)}
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
      {/* Tables ignore the clip of .sr-only, so a wrapper hides it. */}
      <div className="sr-only">
        <table>
          <caption>{title}</caption>
          <thead>
            <tr>
              <th>{xLabel}</th>
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
      </div>
    </figure>
  );
}
