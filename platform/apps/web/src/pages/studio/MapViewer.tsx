import { useEffect, useRef, useState } from 'react';
import type { Artifact } from './types.ts';
export function MapViewer({
  artifact,
  marker,
  dimensions,
  onReady,
}: {
  artifact: Artifact;
  marker?: { x: number; y: number };
  dimensions?: { width: number; height: number };
  onReady?: (url: string) => void;
}) {
  const [loaded, setLoaded] = useState<{
    url: string;
    label: string;
    width: number;
    height: number;
    artifact: Pick<Artifact, 'stage' | 'width' | 'height'>;
    previous?: { url: string; label: string };
  }>();
  const readyCallback = useRef(onReady);
  useEffect(() => {
    readyCallback.current = onReady;
  }, [onReady]);
  const surface = useRef<HTMLDivElement>(null);
  const [viewport, setViewport] = useState({ width: 300, height: 300 });
  const { url, label, stage, width: artifactWidth, height: artifactHeight } = artifact;
  useEffect(() => {
    const el = surface.current;
    if (!el || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(() =>
      setViewport({
        width: Math.max(1, el.clientWidth - 24),
        height: Math.max(1, el.clientHeight - 66),
      }),
    );
    observer.observe(el);
    return () => observer.disconnect();
  }, []);
  const [errorUrl, setErrorUrl] = useState<string>();
  const error = errorUrl === url;
  const [attempt, setAttempt] = useState(0);
  const [zoom, setZoom] = useState(1);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const drag = useRef<{ x: number; y: number; startX: number; startY: number } | undefined>(
    undefined,
  );
  useEffect(() => {
    let cancelled = false;
    const image = new Image();
    image.onload = () => {
      void (image.decode?.() ?? Promise.resolve())
        .catch(() => {})
        .then(() => {
          if (!cancelled) {
            setLoaded((previous) => ({
              url,
              label,
              width: image.naturalWidth,
              height: image.naturalHeight,
              artifact: {
                stage,
                width: artifactWidth,
                height: artifactHeight,
              },
              previous:
                previous?.url === url
                  ? previous.previous
                  : previous
                    ? { url: previous.url, label: previous.label }
                    : undefined,
            }));
            setErrorUrl(undefined);
            readyCallback.current?.(url);
          }
        });
    };
    image.onerror = () => {
      if (!cancelled) setErrorUrl(url);
    };
    image.src = url;
    return () => {
      cancelled = true;
      image.onload = null;
      image.onerror = null;
    };
  }, [url, label, stage, artifactWidth, artifactHeight, attempt]);
  const loadedUrl = loaded?.url;
  useEffect(() => {
    if (!loadedUrl) return;
    const reduced =
      typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
    const timer = setTimeout(
      () =>
        setLoaded((current) =>
          current?.url === loadedUrl ? { ...current, previous: undefined } : current,
        ),
      reduced ? 0 : 450,
    );
    return () => clearTimeout(timer);
  }, [loadedUrl]);
  const fit = loaded ? Math.min(viewport.width / loaded.width, viewport.height / loaded.height) : 1;
  return (
    <figure className="ms-map-viewer">
      <div className="ms-map-tools">
        <span>
          {(loaded?.artifact.stage ?? artifact.stage) === 'ready'
            ? 'PLAYABLE MAP'
            : 'INTERMEDIATE IMAGE'}
        </span>
        <div>
          <button
            aria-label="Zoom out"
            disabled={zoom <= 1}
            onClick={() => setZoom((z) => Math.max(1, z - 0.25))}
          >
            −
          </button>
          <span>{Math.round(zoom * 100)}%</span>
          <button
            aria-label="Zoom in"
            disabled={zoom >= 4}
            onClick={() => setZoom((z) => Math.min(4, z + 0.25))}
          >
            ＋
          </button>
          <button
            onClick={() => {
              setZoom(1);
              setOffset({ x: 0, y: 0 });
            }}
          >
            Fit
          </button>
        </div>
      </div>
      <div
        className="ms-map-surface"
        style={{ touchAction: zoom > 1 ? 'none' : 'pan-y' }}
        ref={surface}
        tabIndex={0}
        aria-label="Map viewport. Use plus and minus to zoom, arrow keys to pan, or zero to fit."
        onKeyDown={(e) => {
          if (
            ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', '+', '=', '-', '0'].includes(e.key)
          ) {
            e.preventDefault();
            if (e.key === '0') {
              setZoom(1);
              setOffset({ x: 0, y: 0 });
            } else if (['+', '=', '-'].includes(e.key))
              setZoom((z) => Math.max(1, Math.min(4, z + (e.key === '-' ? -0.25 : 0.25))));
            else
              setOffset((v) => ({
                x: v.x + (e.key === 'ArrowLeft' ? 24 : e.key === 'ArrowRight' ? -24 : 0),
                y: v.y + (e.key === 'ArrowUp' ? 24 : e.key === 'ArrowDown' ? -24 : 0),
              }));
          }
        }}
        onPointerDown={(e) => {
          if (zoom <= 1) return;
          e.currentTarget.setPointerCapture(e.pointerId);
          drag.current = { x: e.clientX, y: e.clientY, startX: offset.x, startY: offset.y };
        }}
        onPointerMove={(e) => {
          if (!drag.current) return;
          setOffset({
            x: Math.max(-600, Math.min(600, drag.current.startX + e.clientX - drag.current.x)),
            y: Math.max(-600, Math.min(600, drag.current.startY + e.clientY - drag.current.y)),
          });
        }}
        onPointerUp={() => {
          drag.current = undefined;
        }}
        onPointerCancel={() => {
          drag.current = undefined;
        }}
      >
        {loaded && (
          <div
            className="ms-map-image"
            style={{
              transform: `translate(${offset.x}px,${offset.y}px) scale(${zoom})`,
              width: loaded.width * fit,
              height: loaded.height * fit,
            }}
          >
            {loaded.previous && (
              <img
                className="ms-previous-image"
                src={loaded.previous.url}
                alt=""
                aria-hidden="true"
                draggable={false}
              />
            )}
            <img key={loaded.url} src={loaded.url} alt={loaded.label} draggable={false} />
            {marker && dimensions && loaded.url === artifact.url && (
              <span
                className="ms-location-marker"
                aria-label={`Map location ${marker.x}, ${marker.y}`}
                style={{
                  left: `${(marker.x / dimensions.width) * 100}%`,
                  top: `${(marker.y / dimensions.height) * 100}%`,
                }}
              >
                ◎
              </span>
            )}
          </div>
        )}
        {(!loaded || loaded.url !== artifact.url) && !error && (
          <div
            className={`ms-image-loading ${loaded ? 'ms-image-loading-overlay' : ''}`}
            role="status"
          >
            <span className="ms-spinner" />
            Loading {artifact.label}…
          </div>
        )}
        {error && (
          <div className="ms-image-loading" role="alert">
            This image could not be loaded.
            <button
              onClick={() => {
                setErrorUrl(undefined);
                setAttempt((n) => n + 1);
              }}
            >
              Retry image
            </button>
          </div>
        )}
      </div>
      <figcaption>
        <span>{loaded?.label ?? artifact.label}</span>
        <span>
          {loaded?.artifact.width && loaded.artifact.height
            ? `${loaded.artifact.width} × ${loaded.artifact.height} preview pixels`
            : 'Drag to explore when zoomed'}
        </span>
      </figcaption>
    </figure>
  );
}
