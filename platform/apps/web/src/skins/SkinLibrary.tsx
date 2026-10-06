import type { SkinCollection, SkinDesign } from '@glob2/protocol';
import { skinAssetUrl } from './assetUrls.ts';
/* WebGL previews use the same texture and material renderer as the workspace. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useState } from 'react';
import { MeshPreview } from './MeshPreview.tsx';
import { DEFAULT_CAMERA } from './geometry.ts';
import { MODELS, decodeMaterials } from './atlas.ts';
import { paintCanvas, type Skin } from './useSkinDocument.ts';

export function SkinLibrary({
  collection,
  error,
  busy,
  onOpen,
  onUse,
  onNew,
  onDuplicate,
  onDelete,
  onShop,
  onRetry,
  message,
}: {
  collection: SkinCollection | null;
  error?: string;
  busy: boolean;
  onOpen: (skin: SkinDesign) => void;
  onUse: (skin: SkinDesign | string | null) => void;
  onNew: () => void;
  onDuplicate: (id: string, name: string) => void;
  onDelete: (skin: SkinDesign) => void;
  onShop: () => void;
  onRetry: () => void;
  message: string;
}) {
  // Bound simultaneous WebGL previews, including on accounts with many designs.
  const [page, setPage] = useState(0);
  const count = (collection?.designs.length ?? 0) + (collection?.presets.length ?? 0);
  const pages = Math.max(1, Math.ceil(count / 6));
  const currentPage = Math.min(page, pages - 1);
  const start = currentPage * 6,
    end = start + 6;
  const designCount = collection?.designs.length ?? 0;
  return (
    <section className="skin-collection" aria-label="My skins">
      <header className="skin-collection-header">
        <div>
          <span className="skin-eyebrow">YOUR COLONY</span>
          <h1>My skins</h1>
          <p className="skin-muted">Create a look. Use it in your next match.</p>
        </div>
        <div>
          <button onClick={onShop}>Shop</button>
          <button className="skin-primary" disabled={busy || !collection} onClick={onNew}>
            New skin
          </button>
        </div>
      </header>
      {collection?.activeArtworkStatus && collection.activeArtworkStatus !== 'ready' && (
        <details className="skin-artwork-note">
          <summary>Compatibility of your active skin</summary>
          <p>
            {collection.activeArtworkStatus === 'pending'
              ? 'Artwork for some older devices is still preparing. Those devices may temporarily show the default colony.'
              : 'Artwork could not be prepared for some older devices. Those devices will use the default colony.'}
          </p>
        </details>
      )}
      {error && (
        <div>
          <p role="alert">{error}</p>
          <button onClick={onRetry}>Retry loading skins</button>
        </div>
      )}
      {!collection && !error && <p role="status">Loading your skins…</p>}
      <div className="skin-library-grid">
        <article>
          <div className="skin-thumbnail skin-default-preview">
            <img src="/skins/thumbs/swarm.png" alt="Default colony" />
          </div>
          <h3>Default colony</h3>
          <p className="skin-card-state">
            {collection?.activeSkinId === null ? 'In use' : 'Original game artwork'}
          </p>
          <button
            disabled={busy || !collection || collection.activeSkinId === null}
            onClick={() => onUse(null)}
          >
            {collection?.activeSkinId === null ? 'In use' : 'Use in game'}
          </button>
        </article>
        {collection?.designs.slice(start, end).map((s) => {
          const active = collection.activeSkinId === s.skinId;
          const changed =
            s.appliedRevision !== s.revision || s.appliedVersionId !== collection.equippedVersionId;
          return (
            <article key={s.skinId} data-skin-id={s.skinId}>
              <button
                className="skin-card-open"
                disabled={busy}
                onClick={() => onOpen(s)}
                aria-label={`Edit ${s.name}`}
              >
                <DraftThumbnail skin={s} />
                <h3>{s.name}</h3>
              </button>
              <p className="skin-card-state">
                {active ? (changed ? 'In use · changes not applied' : 'In use') : 'Saved'}
              </p>
              <div className="skin-card-actions">
                <button
                  className="skin-primary"
                  disabled={busy || (active && !changed)}
                  onClick={() => onUse(s)}
                >
                  {active && !changed ? 'In use' : 'Use in game'}
                </button>
                <details>
                  <summary aria-label={`More actions for ${s.name}`}>•••</summary>
                  <button disabled={busy} onClick={() => onDuplicate(s.skinId, s.name)}>
                    Duplicate
                  </button>
                  <button disabled={busy} onClick={() => onDelete(s)}>
                    Delete
                  </button>
                </details>
              </div>
            </article>
          );
        })}
        {collection?.presets
          .slice(Math.max(0, start - designCount), Math.max(0, end - designCount))
          .map((s) => (
            <article key={s.skinId}>
              <SkinThumbnail skin={{ ...s, kind: 'preset', entitlement: '' }} />
              <h3>{s.name}</h3>
              <p className="skin-card-state">
                {collection.activeSkinId === s.skinId ? 'In use' : 'Owned'}
              </p>
              <div className="skin-card-actions">
                <button
                  disabled={busy || collection.equippedVersionId === s.id}
                  onClick={() => onUse(s.id)}
                >
                  {collection.equippedVersionId === s.id ? 'In use' : 'Use in game'}
                </button>
                <button disabled={busy} onClick={() => onDuplicate(s.skinId, s.name)}>
                  Customize
                </button>
              </div>
            </article>
          ))}
      </div>
      {collection && !collection.designs.length && (
        <p className="skin-muted">
          Your first design starts with New skin. Your work saves automatically.
        </p>
      )}
      {pages > 1 && (
        <nav className="skin-pagination" aria-label="Skin collection pages">
          <button disabled={busy || currentPage === 0} onClick={() => setPage(currentPage - 1)}>
            Previous
          </button>
          <span>
            {currentPage + 1} / {pages}
          </span>
          <button
            disabled={busy || currentPage + 1 >= pages}
            onClick={() => setPage(currentPage + 1)}
          >
            Next
          </button>
        </nav>
      )}
      <p role="status">{message}</p>
    </section>
  );
}
function DraftThumbnail({ skin }: { skin: SkinDesign }) {
  const [loaded, setLoaded] = useState<{ canvas: HTMLCanvasElement; materials: Uint8Array } | null>(
    null,
  );
  const [error, setError] = useState(false);
  useEffect(() => {
    let cancelled = false;
    void Promise.all([
      (async () => {
        const image = new Image();
        image.src = `data:image/webp;base64,${skin.imageBase64}`;
        await image.decode();
        const canvas = paintCanvas();
        canvas.getContext('2d')!.drawImage(image, 0, 0);
        return canvas;
      })(),
      decodeMaterials(`data:image/webp;base64,${skin.materialBase64}`),
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
  }, [skin.imageBase64, skin.materialBase64]);
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
