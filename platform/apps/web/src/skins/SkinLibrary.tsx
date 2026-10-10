import { displayMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import type { SkinCollection, SkinDesign } from '@glob2/protocol';
import { skinAssetUrl } from './assetUrls.ts';
/* WebGL previews use the same texture and material renderer as the workspace. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useState } from 'react';
import {
  LibraryHeader,
  LibraryGrid,
  LibraryCard,
  LibraryResults,
  LibraryEmpty,
} from '../components/library.tsx';
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
  useLocale();
  // Bound simultaneous WebGL previews, including on accounts with many designs.
  const [page, setPage] = useState(0);
  const count = (collection?.designs.length ?? 0) + (collection?.presets.length ?? 0);
  const pages = Math.max(1, Math.ceil(count / 6));
  const currentPage = Math.min(page, pages - 1);
  const start = currentPage * 6,
    end = start + 6;
  const designCount = collection?.designs.length ?? 0;
  return (
    <section className="skin-collection library-page" aria-label={t('My skins')}>
      <LibraryHeader
        art="swarm"
        title={t('My skins')}
        description={t('Create a look. Use it in your next match.')}
        actions={
          <>
            <button className="primary" disabled={busy || !collection} onClick={onNew}>
              {t('New skin')}
            </button>
            <button onClick={onShop}>{t('Shop')}</button>
          </>
        }
      />
      {collection?.activeArtworkStatus && collection.activeArtworkStatus !== 'ready' && (
        <details className="library-help">
          <summary>{t('Compatibility of your active skin')}</summary>
          <p>
            {collection.activeArtworkStatus === 'pending'
              ? t(
                  'Artwork for some older devices is still preparing. Those devices may temporarily show the default colony.',
                )
              : t(
                  'Artwork could not be prepared for some older devices. Those devices will use the default colony.',
                )}
          </p>
        </details>
      )}
      <LibraryResults
        count={() => Math.min(6, count - start) + 1}
        load={
          error
            ? { status: 'error', error: new Error(displayMessage(error)) }
            : collection
              ? { status: 'ready', data: collection }
              : { status: 'loading' }
        }
        retry={onRetry}
      >
        {() => (
          <>
            <LibraryGrid>
              <LibraryCard>
                <div className="skin-thumbnail skin-default-preview">
                  <img src="/skins/thumbs/swarm.png" alt={t('Default colony')} />
                </div>
                <h2>{t('Default colony')}</h2>
                <p className="skin-card-state">
                  {collection?.activeSkinId === null ? t('In use') : t('Original game artwork')}
                </p>
                <button
                  disabled={busy || !collection || collection.activeSkinId === null}
                  onClick={() => onUse(null)}
                >
                  {collection?.activeSkinId === null ? t('In use') : t('Use in game')}
                </button>
              </LibraryCard>
              {collection?.designs.slice(start, end).map((s) => {
                const active = collection.activeSkinId === s.skinId;
                const changed =
                  s.appliedRevision !== s.revision ||
                  s.appliedVersionId !== collection.equippedVersionId;
                return (
                  <LibraryCard key={s.skinId} skinId={s.skinId}>
                    <button
                      className="skin-card-open"
                      disabled={busy}
                      onClick={() => onOpen(s)}
                      aria-label={t('Edit {value0}', { value0: s.name })}
                    >
                      <DraftThumbnail skin={s} />
                      <h2>{s.name}</h2>
                    </button>
                    <p className="skin-card-state">
                      {active
                        ? changed
                          ? t('In use · changes not applied')
                          : t('In use')
                        : t('Saved')}
                    </p>
                    <div className="skin-card-actions">
                      <button
                        className={active && !changed ? '' : 'primary'}
                        disabled={busy || (active && !changed)}
                        onClick={() => onUse(s)}
                      >
                        {active && !changed ? t('In use') : t('Use in game')}
                      </button>
                      <details>
                        <summary aria-label={t('More actions for {value0}', { value0: s.name })}>
                          {t('•••')}
                        </summary>
                        <button disabled={busy} onClick={() => onDuplicate(s.skinId, s.name)}>
                          {t('Duplicate')}
                        </button>
                        <button disabled={busy} onClick={() => onDelete(s)}>
                          {t('Delete')}
                        </button>
                      </details>
                    </div>
                  </LibraryCard>
                );
              })}
              {collection?.presets
                .slice(Math.max(0, start - designCount), Math.max(0, end - designCount))
                .map((s) => (
                  <LibraryCard key={s.skinId}>
                    <SkinThumbnail skin={{ ...s, kind: 'preset', entitlement: '' }} />
                    <h2>{s.name}</h2>
                    <p className="skin-card-state">
                      {collection.activeSkinId === s.skinId ? t('In use') : t('Owned')}
                    </p>
                    <div className="skin-card-actions">
                      <button
                        className={collection.equippedVersionId === s.id ? '' : 'primary'}
                        disabled={busy || collection.equippedVersionId === s.id}
                        onClick={() => onUse(s.id)}
                      >
                        {collection.equippedVersionId === s.id ? t('In use') : t('Use in game')}
                      </button>
                      <button disabled={busy} onClick={() => onDuplicate(s.skinId, s.name)}>
                        {t('Customize')}
                      </button>
                    </div>
                  </LibraryCard>
                ))}
            </LibraryGrid>
            {collection && !collection.designs.length && (
              <LibraryEmpty
                art="swarm"
                action={
                  <button className="primary" disabled={busy} onClick={onNew}>
                    {t('Create')}
                  </button>
                }
              >
                {t('Your first design starts with New skin. Your work saves automatically.')}
              </LibraryEmpty>
            )}
            {pages > 1 && (
              <nav className="skin-pagination" aria-label={t('Skin collection pages')}>
                <button
                  disabled={busy || currentPage === 0}
                  onClick={() => setPage(currentPage - 1)}
                >
                  {t('Previous')}
                </button>
                <span>
                  <RichMessage
                    source={'{slot0} / {slot1}'}
                    slots={{ slot0: currentPage + 1, slot1: pages }}
                  />
                </span>
                <button
                  disabled={busy || currentPage + 1 >= pages}
                  onClick={() => setPage(currentPage + 1)}
                >
                  {t('Next')}
                </button>
              </nav>
            )}
            <p role="status">{message}</p>
          </>
        )}
      </LibraryResults>
    </section>
  );
}
function DraftThumbnail({ skin }: { skin: SkinDesign }) {
  useLocale();
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
          label={t('{value0} rendered preview', { value0: skin.name })}
        />
      ) : (
        <span>{error ? t('Preview unavailable') : t('Loading preview…')}</span>
      )}
    </div>
  );
}
function SkinThumbnail({ skin }: { skin: Skin }) {
  useLocale();
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
          label={t('{value0} rendered preview', { value0: skin.name })}
        />
      ) : (
        <span>{error ? t('Preview unavailable') : t('Loading preview…')}</span>
      )}
    </div>
  );
}
