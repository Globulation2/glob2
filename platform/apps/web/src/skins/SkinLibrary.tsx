import { skinAssetUrl } from './assetUrls.ts';
/* WebGL previews use the same texture and material renderer as the workspace. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useState } from 'react';
import { MeshPreview } from './MeshPreview.tsx';
import { DEFAULT_CAMERA } from './geometry.ts';
import { MODELS, decodeMaterials } from './atlas.ts';
import { SWARM_SHAPES } from './swarmShapes.ts';
import { paintCanvas, type Skin, type Catalog } from './useSkinDocument.ts';

export function SkinLibrary({
  catalog,
  error,
  busy,
  onOpen,
  onEquip,
  message,
}: {
  catalog: Catalog | null;
  error?: string;
  busy: boolean;
  onOpen: (skin: Skin) => void;
  onEquip: (id: string | null) => void;
  message: string;
}) {
  const [page, setPage] = useState(0);
  const items = catalog?.items ?? [];
  return (
    <>
      <p className="skin-muted">
        Published versions stay available. Equip a look for your next match.
      </p>
      <button disabled={busy} onClick={() => onEquip(null)}>
        Use default colony
      </button>
      {error && <p role="alert">{error}</p>}
      {!items.length && <p>No published skins yet. Your draft is waiting in the studio.</p>}
      <div className="skin-library-grid">
        {items.slice(page * 6, page * 6 + 6).map((s) => (
          <article key={s.id} data-version-id={s.id}>
            <SkinThumbnail skin={s} />
            <h3>{s.name}</h3>
            <p className="skin-muted">
              {SWARM_SHAPES[s.swarmMesh].name} · {s.swarmViewAngle ?? 0}°
            </p>
            <button disabled={busy} onClick={() => onOpen(s)}>
              {s.kind === 'custom' ? 'Edit this version' : 'Use as a starting point'}
            </button>
            <button
              className="skin-primary"
              disabled={busy || catalog?.equippedVersionId === s.id}
              onClick={() => onEquip(s.id)}
            >
              {catalog?.equippedVersionId === s.id ? 'Equipped' : 'Equip'}
            </button>
          </article>
        ))}
      </div>
      {items.length > 6 && (
        <div className="skin-pagination">
          <button disabled={!page} onClick={() => setPage((p) => p - 1)}>
            Previous
          </button>
          <span>
            {page + 1}/{Math.ceil(items.length / 6)}
          </span>
          <button disabled={(page + 1) * 6 >= items.length} onClick={() => setPage((p) => p + 1)}>
            Next
          </button>
        </div>
      )}
      <p role="status">{message}</p>
    </>
  );
}
function SkinThumbnail({ skin }: { skin: Skin }) {
  const textureUrl = skinAssetUrl(skin, 'texture');
  const materialUrl = skinAssetUrl(skin, 'material');
  const [loaded, setLoaded] = useState<{ canvas: HTMLCanvasElement; materials: Uint8Array } | null>(
      null,
    ),
    [error, setError] = useState(false);
  useEffect(() => {
    let cancelled = false;
    void Promise.all([
      (async () => {
        const image = new Image();
        image.src = textureUrl;
        await image.decode();
        const canvas = paintCanvas();
        canvas.getContext('2d')!.drawImage(image, 0, 0);
        return canvas;
      })(),
      decodeMaterials(materialUrl),
    ])
      .then(([canvas, materials]) => {
        if (!cancelled) setLoaded({ canvas, materials });
      })
      .catch(() => {
        if (!cancelled) setError(true);
      });
    return () => {
      cancelled = true;
    };
  }, [textureUrl, materialUrl]);
  return (
    <div className="skin-thumbnail">
      {loaded ? (
        <MeshPreview
          texture={loaded.canvas}
          materials={loaded.materials}
          revision={1}
          model={MODELS[3]}
          swarmMesh={skin.swarmMesh}
          camera={{ ...DEFAULT_CAMERA, game: true, angle: skin.swarmViewAngle ?? 0 }}
          action=""
          phase={0}
          interactive={false}
          label={`${skin.name} rendered preview`}
        />
      ) : (
        <span>{error ? 'Preview unavailable' : 'Loading preview…'}</span>
      )}
    </div>
  );
}
