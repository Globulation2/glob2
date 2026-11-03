import { t, useLocale } from '../i18n.tsx';
import { useId, useRef, useState } from 'react';
import { MapImage } from './common.tsx';

/** One complete map period, with view-only panning across both torus seams. */
export function MapPreview({
  src,
  alt,
  frameInset = 0,
}: {
  src: string | undefined;
  alt: string;
  /** Decorative pixels baked into the source, excluded from the wrapping texture. */
  frameInset?: number;
}) {
  useLocale();
  return src ? (
    <LoadedMapPreview key={src} src={src} alt={alt} frameInset={frameInset} />
  ) : (
    <MapImage src={src} alt={alt} />
  );
}

const wrap = (value: number) => value - Math.floor(value);

function LoadedMapPreview({
  src,
  alt,
  frameInset,
}: {
  src: string;
  alt: string;
  frameInset: number;
}) {
  useLocale();
  const helpId = useId();
  const [image, setImage] = useState<{ width: number; height: number }>();
  const [failed, setFailed] = useState(false);
  const [attempt, setAttempt] = useState(0);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const drag = useRef<{ pointer: number; x: number; y: number } | undefined>(undefined);
  const pan = (x: number, y: number) =>
    setOffset((previous) => ({ x: wrap(previous.x + x), y: wrap(previous.y + y) }));
  const stop = () => {
    drag.current = undefined;
    setDragging(false);
  };

  const inset = image
    ? Math.max(0, Math.min(frameInset, (Math.min(image.width, image.height) - 1) / 2))
    : 0;
  return (
    <figure className="map-preview">
      <div
        className={`map-preview-surface${dragging ? ' dragging' : ''}`}
        style={{ aspectRatio: image ? `${image.width} / ${image.height}` : '1' }}
        role="group"
        aria-label={alt}
        aria-describedby={helpId}
        tabIndex={image ? 0 : undefined}
        onKeyDown={(event) => {
          if (!image) return;
          const moves: Record<string, [number, number]> = {
            ArrowLeft: [-0.05, 0],
            ArrowRight: [0.05, 0],
            ArrowUp: [0, -0.05],
            ArrowDown: [0, 0.05],
          };
          const move = moves[event.key];
          if (move) {
            event.preventDefault();
            pan(...move);
          } else if (event.key === 'Home') {
            event.preventDefault();
            setOffset({ x: 0, y: 0 });
          }
        }}
        onPointerDown={(event) => {
          if (!image || event.button !== 0 || drag.current) return;
          event.preventDefault();
          event.currentTarget.focus({ preventScroll: true });
          event.currentTarget.setPointerCapture(event.pointerId);
          drag.current = { pointer: event.pointerId, x: event.clientX, y: event.clientY };
          setDragging(true);
        }}
        onPointerMove={(event) => {
          const current = drag.current;
          if (!current || current.pointer !== event.pointerId) return;
          pan(
            (event.clientX - current.x) / event.currentTarget.clientWidth,
            (event.clientY - current.y) / event.currentTarget.clientHeight,
          );
          drag.current = { pointer: current.pointer, x: event.clientX, y: event.clientY };
        }}
        onPointerUp={(event) => {
          if (drag.current?.pointer !== event.pointerId) return;
          stop();
          event.currentTarget.releasePointerCapture(event.pointerId);
        }}
        onPointerCancel={stop}
        onLostPointerCapture={stop}
      >
        <img
          key={attempt}
          className="map-preview-loader"
          src={src}
          alt=""
          aria-hidden="true"
          onLoad={(event) => {
            setImage({
              width: event.currentTarget.naturalWidth,
              height: event.currentTarget.naturalHeight,
            });
          }}
          onError={() => setFailed(true)}
        />
        {image &&
          [-1, 0].flatMap((y) =>
            [-1, 0].map((x) => (
              <div
                key={`${x},${y}`}
                className="map-preview-tile"
                aria-hidden="true"
                style={{ left: `${(x + offset.x) * 100}%`, top: `${(y + offset.y) * 100}%` }}
              >
                <img
                  src={src}
                  alt=""
                  draggable={false}
                  style={{
                    width: `${(image.width / (image.width - 2 * inset)) * 100}%`,
                    height: `${(image.height / (image.height - 2 * inset)) * 100}%`,
                    left: `${(-inset / (image.width - 2 * inset)) * 100}%`,
                    top: `${(-inset / (image.height - 2 * inset)) * 100}%`,
                  }}
                />
              </div>
            )),
          )}
        {!image && (
          <div className="map-preview-status" role="status">
            {failed ? t('This preview could not be loaded.') : t('Loading map preview…')}
            {failed && (
              <button
                onClick={() => {
                  setFailed(false);
                  setAttempt((n) => n + 1);
                }}
              >
                {t('Retry preview')}
              </button>
            )}
          </div>
        )}
      </div>
      <figcaption>
        <span id={helpId}>{t('Drag or use arrow keys to explore the wraparound map.')}</span>
        <button
          disabled={!image}
          onClick={() => {
            stop();
            setOffset({ x: 0, y: 0 });
          }}
        >
          {t('Reset view')}
        </button>
      </figcaption>
    </figure>
  );
}
