import { message as sourceMessage } from '../messages.ts';
import { statusLabel } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
/* DOM nodes are present during pointer events; model catalogs are nonempty. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import {
  SWARM_MESHES,
  type SwarmMeshId,
  type SkinCollection,
  type SkinDesign,
} from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';
import { MeshPreview, type SceneView, type Tool } from '../skins/MeshPreview.tsx';
import { ACTIONS, DEFAULT_CAMERA, type Camera } from '../skins/geometry.ts';
import { MODELS, type Model } from '../skins/atlas.ts';
import { SWARM_SHAPES, swarmModel } from '../skins/swarmShapes.ts';
import { SkinStore } from '../skins/Store.tsx';
import { StudioDialog, StudioIcon, MaterialSwatches } from '../skins/StudioControls.tsx';
import { PatternDialog } from '../skins/PatternDialog.tsx';
import { applyCoverage } from '../skins/paint.ts';
import { useSkinDocument } from '../skins/useSkinDocument.ts';
import { useToolboxLayout } from '../skins/useToolboxLayout.ts';
import { SkinLibrary } from '../skins/SkinLibrary.tsx';
import { ColorPicker } from '../skins/ColorPicker.tsx';
import { CopyPaintDialog } from '../skins/CopyPaintDialog.tsx';

export function Skins() {
  useLocale();
  const { account } = useSession();
  return <SkinStudio key={account?.id ?? 'local'} />;
}
type Dialog = 'patterns' | 'shop' | 'delete' | 'shapes' | 'copy' | null;
function SkinStudio() {
  useLocale();
  const { account } = useSession();
  const doc = useSkinDocument(account?.kind === 'registered' ? account.id : undefined),
    d = doc.data;
  const [model, setModel] = useState<Model>(MODELS[0]),
    [action, setAction] = useState('walk'),
    [phase, setPhase] = useState(0),
    [animate, setAnimate] = useState(false);
  const [camera, setCamera] = useState<Camera>(DEFAULT_CAMERA),
    [tool, setTool] = useState<Tool>('brush');
  const [mode, setMode] = useState<'colour' | 'material'>('colour'),
    [brush, setBrush] = useState('#ed9252'),
    [material, setMaterial] = useState(0);
  const [size, setSize] = useState(36),
    [opacity, setOpacity] = useState(1),
    [hardness, setHardness] = useState(0.8),
    [pressure, setPressure] = useState(false);
  const [dialog, setDialog] = useState<Dialog>(() =>
    new URLSearchParams(window.location.search).has('purchase') ? 'shop' : null,
  );
  const [library, setLibrary] = useState(account?.kind === 'registered');
  const [deleting, setDeleting] = useState<SkinDesign | null>(null);
  const [collectionBusy, setCollectionBusy] = useState(false);
  const [dismissedConflict, setDismissedConflict] = useState<string | null>(null);
  const [finalView, setFinalView] = useState(false),
    [finalAngle, setFinalAngle] = useState(0),
    [reference, setReference] = useState(false);
  const { toolbox, position, collapsed, setCollapsed, handleProps, resetLayout } = useToolboxLayout(
    !finalView && !library,
  );
  const scene = useRef<SceneView | null>(null);
  const [cachedCollection, setCachedCollection] = useState<SkinCollection | null>(null);
  const catalog = useLoad(
    async (signal) =>
      account?.kind === 'registered'
        ? request<SkinCollection>('GET', '/api/v1/skins/collection', { signal }).then((data) => {
            if (!signal.aborted) setCachedCollection(data);
            return data;
          })
        : null,
    [account?.id],
  );
  const collection = catalog.status === 'ready' ? catalog.data : cachedCollection;
  const reloadCatalog = catalog.reload;
  useEffect(() => {
    window.addEventListener('online', reloadCatalog);
    return () => window.removeEventListener('online', reloadCatalog);
  }, [reloadCatalog]);
  useEffect(() => {
    if (collection?.activeArtworkStatus !== 'pending') return;
    const timer = window.setInterval(reloadCatalog, 5000);
    return () => window.clearInterval(timer);
  }, [collection?.activeArtworkStatus, reloadCatalog]);
  const active = collection?.activeSkinId === d.skinId;
  const saved = collection?.designs.find((s) => s.skinId === d.skinId);
  const applied =
    active &&
    !doc.hasChanges &&
    saved?.appliedRevision === doc.savedRevision &&
    saved?.appliedVersionId === collection?.equippedVersionId;
  const imported = useRef(false);
  useEffect(() => {
    if (
      account?.kind === 'registered' &&
      collection &&
      doc.hydrated &&
      doc.hasChanges &&
      !d.skinId &&
      !imported.current
    ) {
      imported.current = true;
      void Promise.resolve().then(async () => {
        setLibrary(false);
        const result = await doc.newDesign(d.name, undefined, true);
        if (result) catalog.reload();
      });
    }
  }, [account?.kind, collection, doc, d.skinId, d.name, catalog]);
  const disabled = doc.busy || !doc.hydrated || collectionBusy;
  async function leaveEditor() {
    doc.finish();
    doc.saveLocal();
    try {
      await doc.flush();
    } catch {
      /* Recovery remains attached to this design. */
    }
    catalog.reload();
    setLibrary(true);
  }
  async function create(name = t('My colony'), sourceSkinId?: string) {
    const result = await doc.newDesign(name, sourceSkinId);
    if (result) {
      setLibrary(false);
      catalog.reload();
    }
  }
  async function applySelection(skin: SkinDesign | string | null) {
    setCollectionBusy(true);
    try {
      if (skin && typeof skin !== 'string') {
        const opened = await doc.openDesign(skin);
        if (!opened) return;
        const used = await doc.useInGame(catalog.reload);
        if (!used) setLibrary(false);
        return;
      } else await request('PUT', '/api/v1/skins/equipped', { body: { versionId: skin } });
      doc.setMessage(sourceMessage('Used for your next match.'));
      catalog.reload();
    } catch (e) {
      doc.setMessage(e instanceof Error ? e : sourceMessage('Could not use this skin.'));
    } finally {
      setCollectionBusy(false);
    }
  }
  async function deleteSkin() {
    if (!deleting) return;
    setCollectionBusy(true);
    try {
      await request('DELETE', `/api/v1/skins/designs/${deleting.skinId}`);
      doc.forgetDesign(deleting.skinId);
      doc.setMessage(sourceMessage('Skin deleted.'));
      setDeleting(null);
      setDialog(null);
      catalog.reload();
    } catch (e) {
      doc.setMessage(e instanceof Error ? e : sourceMessage('Could not delete this skin.'));
    } finally {
      setCollectionBusy(false);
    }
  }
  function showDialog(next: Dialog) {
    doc.finish();
    setAnimate(false);
    if (scene.current) setPhase(scene.current.frame);
    setDialog(next);
  }
  function chooseModel(m: Model) {
    doc.finish();
    setModel(m);
    setAction(ACTIONS[m.id][0] ?? '');
    setPhase(0);
    setAnimate(false);
    setFinalView(false);
    setCamera(DEFAULT_CAMERA);
  }
  function selectShape(shape: SwarmMeshId) {
    doc.edit({ swarmMesh: shape });
    chooseModel(MODELS[3]);
    setDialog(null);
  }
  function enterFinal() {
    doc.finish();
    setAnimate(false);
    setFinalAngle(d.swarmViewAngle);
    setFinalView(true);
  }
  function pick(index: number) {
    const at = (model.y + Math.floor(index / 256)) * 512 + model.x + (index % 256);
    if (mode === 'material') setMaterial(d.materials[at] ?? 0);
    else
      setBrush(
        '#' +
          Array.from(d.colour.subarray(at * 4, at * 4 + 3), (v) =>
            v.toString(16).padStart(2, '0'),
          ).join(''),
      );
    setTool('brush');
  }
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (
        library ||
        dialog ||
        disabled ||
        (e.target instanceof HTMLElement &&
          (e.target.matches('input,textarea,select') || e.target.isContentEditable))
      )
        return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        doc.history(!e.shiftKey);
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') {
        e.preventDefault();
        doc.history(false);
      } else if (e.ctrlKey || e.metaKey || e.altKey) return;
      else if (e.key === '[') setSize((s) => Math.max(4, s - 4));
      else if (e.key === ']') setSize((s) => Math.min(160, s + 4));
      else if (e.key === 'b') setTool('brush');
      else if (e.key === 'e') setTool('erase');
      else if (e.key === 'i') setTool('pick');
      else if (e.key === 'f' && !finalView) setCamera(DEFAULT_CAMERA);
      else if (!finalView && (e.key === '+' || e.key === '=' || e.key === '-')) {
        e.preventDefault();
        setCamera((c) => ({
          ...c,
          zoom: Math.max(0.225, Math.min(4, c.zoom * (e.key === '-' ? 0.8 : 1.25))),
        }));
      }
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [doc, dialog, disabled, finalView, library]);
  const common = {
    texture: doc.canvas,
    materials: d.materials,
    revision: doc.revision,
    model,
    swarmMesh: d.swarmMesh,
    action,
    phase,
  };
  return (
    <div
      className="skin-studio"
      data-toolbox={collapsed ? 'collapsed' : 'expanded'}
      data-mode={finalView ? 'final' : 'paint'}
    >
      {library ? (
        <SkinLibrary
          collection={collection}
          error={catalog.status === 'error' ? catalog.error.message : undefined}
          busy={disabled}
          onOpen={(skin) => {
            void doc.openDesign(skin).then((opened) => {
              if (opened) setLibrary(false);
            });
          }}
          onUse={(skin) => void applySelection(skin)}
          onNew={() => void create()}
          onDuplicate={(id, name) =>
            void create(t('{value0} copy', { value0: name.slice(0, 59) }), id)
          }
          onDelete={(skin) => {
            setDeleting(skin);
            showDialog('delete');
          }}
          onShop={() => showDialog('shop')}
          onRetry={catalog.reload}
          message={doc.message}
        />
      ) : (
        <>
          <header className="skin-topbar">
            {account?.kind === 'registered' ? (
              <button
                className="skin-icon-button"
                aria-label={t('Back to My skins')}
                onClick={() => void leaveEditor()}
                disabled={disabled}
              >
                <StudioIcon name="back" />
              </button>
            ) : (
              <a
                href="/"
                className="skin-icon-button"
                aria-label={t('Back to Globulation 2')}
                onClick={() => doc.saveLocal()}
              >
                <StudioIcon name="back" />
              </a>
            )}
            <div className="skin-document-title">
              <span className="skin-eyebrow">{t('COLONY STUDIO')}</span>
              <input
                aria-label={t('Skin name')}
                maxLength={64}
                value={d.name}
                disabled={disabled}
                onChange={(e) => doc.edit({ name: e.target.value })}
                onBlur={(e) => {
                  const name = e.target.value.trim() || 'Untitled skin';
                  if (name !== d.name) doc.edit({ name });
                }}
              />
              <small className="skin-mobile-status" role="status">
                {statusLabel(doc.status)}
              </small>
            </div>
            <span className="skin-save-status" role="status">
              {statusLabel(doc.status)}
              {active && !applied
                ? t(' · Changes not applied')
                : applied
                  ? t(' · Used for your next match')
                  : ''}
            </span>
            <div className="skin-history">
              <button
                className="skin-icon-button"
                aria-label={t('Undo')}
                disabled={disabled || !doc.canUndo}
                onClick={() => doc.history(true)}
              >
                <StudioIcon name="undo" />
              </button>
              <button
                className="skin-icon-button"
                aria-label={t('Redo')}
                disabled={disabled || !doc.canRedo}
                onClick={() => doc.history(false)}
              >
                <StudioIcon name="redo" />
              </button>
            </div>
            <nav aria-label={t('Studio')}>
              {account?.kind !== 'registered' ? (
                <a href="/signin" onClick={() => doc.saveLocal()}>
                  {t('Sign in to save')}
                </a>
              ) : collection && !collection.canUseCustom ? (
                <a
                  href="#"
                  onClick={(e) => {
                    e.preventDefault();
                    showDialog('shop');
                  }}
                >
                  {t('Unlock designer')}
                </a>
              ) : (
                <button
                  className="skin-primary"
                  disabled={disabled || doc.conflict || applied}
                  onClick={() => void doc.useInGame(catalog.reload)}
                >
                  {applied ? t('In use') : t('Use in game')}
                </button>
              )}
            </nav>
          </header>
          <section className="skin-stage" aria-label={t('Skin designer')}>
            <MeshPreview
              {...common}
              camera={finalView ? { ...DEFAULT_CAMERA, game: true, angle: finalAngle } : camera}
              onCamera={(c) => (finalView ? setFinalAngle(c.angle) : setCamera(c))}
              animate={animate && !dialog && !finalView}
              active={!dialog}
              interactive={!disabled}
              tool={finalView ? 'orbit' : tool}
              size={size}
              hardness={mode === 'material' ? 1 : hardness}
              pressure={pressure}
              onPause={(frame) => {
                setAnimate(false);
                setPhase(frame);
              }}
              onScene={(s) => {
                scene.current = s;
              }}
              onCoverage={(coverage, p) => {
                if (disabled || finalView) return;
                doc.paint((data) =>
                  applyCoverage(
                    data,
                    model,
                    coverage,
                    mode,
                    tool === 'erase' ? '#ffffff' : brush,
                    tool === 'erase' ? 0 : material,
                    mode === 'material' ? 1 : opacity * p,
                  ),
                );
              }}
              onEnd={(cancel) => doc.finish(cancel)}
              onPick={pick}
            />
            <div className="skin-models skin-panel" role="group" aria-label={t('Model to paint')}>
              {MODELS.map((m) => (
                <button
                  key={m.id}
                  aria-pressed={model.id === m.id}
                  disabled={disabled}
                  onClick={() => chooseModel(m)}
                >
                  <img src={`/skins/thumbs/${m.mesh}.png`} alt="" />
                  <span>{m.name}</span>
                </button>
              ))}
            </div>
            {model.id === 'swarm' && (
              <button className="skin-shape-button skin-panel" onClick={() => showDialog('shapes')}>
                {SWARM_SHAPES[d.swarmMesh].name}
                <span>{t('Change shape ⌄')}</span>
              </button>
            )}
            {!finalView && (
              <aside
                ref={toolbox}
                className={`skin-toolbox skin-panel ${collapsed ? 'is-collapsed' : ''}`}
                aria-label={t('Paint tools')}
                style={position ? { left: position.x, top: position.y } : undefined}
              >
                <div className="skin-toolbox-handle" {...handleProps}>
                  <span>
                    <RichMessage
                      source={'⠿ {slot0}'}
                      slots={{ slot0: <span>{t('TOOLBOX')}</span> }}
                    />
                  </span>
                  <details className="skin-toolbox-menu">
                    <summary aria-label={t('Toolbox options')}>{t('•••')}</summary>
                    <button onClick={resetLayout}>{t('Reset toolbox layout')}</button>
                  </details>
                  <button
                    aria-label={collapsed ? t('Expand toolbox') : t('Collapse toolbox')}
                    onClick={() => setCollapsed(!collapsed)}
                  >
                    {collapsed ? '+' : '−'}
                  </button>
                </div>
                <div className="skin-tools">
                  {(
                    [
                      ['brush', t('Brush'), 'B'],
                      ['erase', t('Eraser'), 'E'],
                      ['pick', t('Eyedropper'), 'I'],
                      ['orbit', t('Rotate'), ''],
                    ] as const
                  ).map(([id, label, key]) => (
                    <button
                      key={id}
                      aria-pressed={tool === id}
                      title={`${label}${key ? ' · ' + key : ''}`}
                      onClick={() => setTool(id)}
                    >
                      <StudioIcon name={id} />
                      <span>{label}</span>
                    </button>
                  ))}
                  <button onClick={() => showDialog('patterns')}>
                    <StudioIcon name="patterns" />
                    <span>{t('Patterns')}</span>
                  </button>
                </div>
                {!collapsed && (
                  <div className="skin-tool-options">
                    <div className="skin-segment" role="group" aria-label={t('Brush paints')}>
                      <button aria-pressed={mode === 'colour'} onClick={() => setMode('colour')}>
                        {t('Color')}
                      </button>
                      <button
                        aria-pressed={mode === 'material'}
                        onClick={() => setMode('material')}
                      >
                        {t('Material')}
                      </button>
                    </div>
                    {mode === 'colour' ? (
                      <ColorPicker label={t('Paint color')} value={brush} onChange={setBrush} />
                    ) : (
                      <MaterialSwatches
                        color={d.building}
                        selected={material}
                        onSelect={setMaterial}
                      />
                    )}
                    <label>
                      {t('Brush size ')}
                      <span className="skin-value">{size}</span>
                      <input
                        type="range"
                        aria-label={t('Brush size ')}
                        min={4}
                        max={160}
                        value={size}
                        onChange={(e) => setSize(Number(e.target.value))}
                      />
                    </label>
                    {mode === 'colour' && (
                      <>
                        <label>
                          {t('Opacity ')}
                          <span className="skin-value">
                            <RichMessage
                              source={'{slot0}%'}
                              slots={{ slot0: Math.round(opacity * 100) }}
                            />
                          </span>
                          <input
                            type="range"
                            aria-label={t('Opacity ')}
                            min={0.05}
                            max={1}
                            step={0.05}
                            value={opacity}
                            onChange={(e) => setOpacity(Number(e.target.value))}
                          />
                        </label>
                        <label>
                          {t('Hardness ')}
                          <span className="skin-value">
                            <RichMessage
                              source={'{slot0}%'}
                              slots={{ slot0: Math.round(hardness * 100) }}
                            />
                          </span>
                          <input
                            type="range"
                            aria-label={t('Hardness ')}
                            min={0}
                            max={1}
                            step={0.05}
                            value={hardness}
                            onChange={(e) => setHardness(Number(e.target.value))}
                          />
                        </label>
                      </>
                    )}
                    <label className="skin-check">
                      <input
                        type="checkbox"
                        checked={pressure}
                        onChange={(e) => setPressure(e.target.checked)}
                      />
                      {t('Pen pressure')}
                    </label>
                    <details className="skin-buildings">
                      <summary>{t('Buildings')}</summary>
                      <ColorPicker
                        label={t('Building color')}
                        value={d.building}
                        onChange={(building) => doc.edit({ building })}
                        description={t('Colors your buildings; painted units stay unchanged.')}
                      />
                      <svg
                        viewBox="0 0 120 80"
                        role="img"
                        aria-label={t('Building color preview')}
                        style={{ color: d.building, height: 48 }}
                      >
                        <path
                          fill="currentColor"
                          d="M18 69V32L36 13l18 19v37zm37 0V17L73 2l18 15v52zm38 0V43l12-13 12 13v26Z"
                        />
                      </svg>
                    </details>
                    <button onClick={() => showDialog('copy')}>
                      {t('Copy paint to other models…')}
                    </button>
                    <details>
                      <summary>{t('Paint repeats on matching surfaces')}</summary>
                      <p>
                        {t(
                          'Brush and eraser reach hidden surfaces underneath the cursor. Some front/back and top/bottom surfaces share paint. A stroke may appear on their matching side too, keeping the glob’s flips seamless.',
                        )}
                      </p>
                    </details>
                  </div>
                )}
              </aside>
            )}
            <div className="skin-navigation skin-panel">
              <div className="skin-view-compass" aria-label={t('View orientation')}>
                <span
                  style={{
                    transform: `rotate(${-(finalView ? finalAngle : (camera.yaw * 180) / Math.PI)}deg)`,
                  }}
                >
                  {t('↑')}
                </span>
                <small>{finalView ? t('FINAL VIEW') : t('VIEW')}</small>
              </div>
              {!finalView && (
                <>
                  <select
                    aria-label={t('Camera view')}
                    value=""
                    onChange={(e) => {
                      if (e.target.value === 'zoom-in' || e.target.value === 'zoom-out') {
                        const factor = e.target.value === 'zoom-in' ? 1.25 : 0.8;
                        setCamera((c) => ({
                          ...c,
                          zoom: Math.max(0.225, Math.min(4, c.zoom * factor)),
                        }));
                        return;
                      }
                      const views: Record<string, number> = {
                        front: 0,
                        back: Math.PI,
                        left: -Math.PI / 2,
                        right: Math.PI / 2,
                      };
                      const yaw = views[e.target.value];
                      if (yaw !== undefined) setCamera({ ...camera, yaw, pitch: 0, game: false });
                    }}
                  >
                    <option value="" disabled>
                      {t('View…')}
                    </option>
                    <optgroup label={t('Inspection zoom')}>
                      <option value="zoom-in">{t('Zoom in (+)')}</option>
                      <option value="zoom-out">{t('Zoom out (−)')}</option>
                    </optgroup>
                    {['front', 'back', 'left', 'right'].map((v) => (
                      <option key={v} value={v}>
                        {v[0]!.toUpperCase() + v.slice(1)}
                      </option>
                    ))}
                  </select>
                  <button
                    className="skin-icon-button"
                    aria-label={t('Fit model')}
                    title={t('Fit model · F')}
                    onClick={() => setCamera(DEFAULT_CAMERA)}
                  >
                    <StudioIcon name="fit" />
                  </button>
                  <button
                    className="skin-icon-button"
                    aria-label={t('Show game view')}
                    title={t('Game-size reference')}
                    aria-pressed={reference}
                    onClick={() => setReference(!reference)}
                  >
                    <StudioIcon name="view" />
                  </button>
                </>
              )}
            </div>
            {reference && !finalView && (
              <div className="skin-reference skin-panel">
                <span className="skin-eyebrow">
                  <RichMessage
                    source={'IN GAME · {slot0}'}
                    slots={{
                      slot0: model.id === 'swarm' ? `${d.swarmViewAngle}°` : t('CURRENT POSE'),
                    }}
                  />
                </span>
                <div
                  className="skin-game-size"
                  style={{
                    width:
                      (model.id === 'swarm'
                        ? 128
                        : model.id === 'worker'
                          ? 38
                          : model.id === 'warrior'
                            ? 40
                            : 32) * 1.25,
                    height:
                      (model.id === 'swarm'
                        ? 128
                        : model.id === 'worker'
                          ? 38
                          : model.id === 'warrior'
                            ? 40
                            : 32) * 1.25,
                  }}
                >
                  <MeshPreview
                    {...common}
                    camera={{
                      ...DEFAULT_CAMERA,
                      game: true,
                      angle: model.id === 'swarm' ? d.swarmViewAngle : 0,
                    }}
                    interactive={false}
                    active={!dialog}
                  />
                </div>
                <small>{t('Actual game size · 1×')}</small>
              </div>
            )}
            {finalView ? (
              <div className="skin-final-view skin-panel">
                <span className="skin-eyebrow">{t('CHOOSE FINAL VIEW')}</span>
                <h2>{t('Your swarm, in the game')}</h2>
                <p>{t('Turn around the ring. Height and scale stay fixed.')}</p>
                <div className="skin-camera-ring" aria-hidden="true">
                  <span style={{ transform: `rotate(${finalAngle}deg)` }}>{t('●')}</span>
                </div>
                <label>
                  {t('Camera angle ')}
                  <span className="skin-value">
                    <RichMessage source={'{slot0}°'} slots={{ slot0: finalAngle }} />
                  </span>
                  <input
                    type="range"
                    min={0}
                    max={359}
                    aria-label={t('Camera angle ')}
                    value={finalAngle}
                    onChange={(e) => setFinalAngle(Number(e.target.value))}
                  />
                </label>
                <div>
                  <button onClick={() => setFinalAngle(0)}>{t('Reset angle')}</button>
                  <button onClick={() => setFinalView(false)}>{t('Cancel')}</button>
                  <button
                    className="skin-primary"
                    onClick={() => {
                      doc.edit({ swarmViewAngle: finalAngle });
                      setFinalView(false);
                      setReference(true);
                    }}
                  >
                    {t('Use this view')}
                  </button>
                </div>
              </div>
            ) : (
              <div className="skin-pose-strip skin-panel">
                {model.id === 'swarm' ? (
                  <>
                    <span className="skin-muted">
                      <RichMessage
                        source={'Final game view · {slot0}°'}
                        slots={{ slot0: d.swarmViewAngle }}
                      />
                    </span>
                    <button onClick={enterFinal}>
                      <StudioIcon name="view" />
                      {t('Choose final view')}
                    </button>
                  </>
                ) : (
                  <>
                    <div className="skin-actions" role="group" aria-label={t('Pose')}>
                      {ACTIONS[model.id].map((a) => (
                        <button
                          key={a}
                          aria-pressed={action === a}
                          onClick={() => {
                            setAction(a);
                            setPhase(0);
                            setAnimate(false);
                          }}
                        >
                          <img src={`/skins/thumbs/${model.id}-${a}.png`} alt="" />
                          <span>{a[0]!.toUpperCase() + a.slice(1)}</span>
                        </button>
                      ))}
                    </div>
                    <button
                      className="skin-icon-button"
                      aria-label={animate ? t('Pause animation') : t('Play animation')}
                      onClick={() => {
                        if (animate && scene.current) setPhase(scene.current.frame);
                        setAnimate(!animate);
                      }}
                    >
                      <StudioIcon name={animate ? 'pause' : 'play'} />
                    </button>
                    <label className="skin-frame">
                      <span>{t('Pose')}</span>
                      <input
                        aria-label={t('Frame')}
                        type="range"
                        min={0}
                        max={31}
                        value={phase}
                        onChange={(e) => {
                          setAnimate(false);
                          setPhase(Number(e.target.value));
                        }}
                      />
                      <span className="skin-value">
                        <RichMessage source={'{slot0}/32'} slots={{ slot0: phase + 1 }} />
                      </span>
                    </label>
                  </>
                )}
              </div>
            )}
            <div className="skin-stage-hint">
              {finalView
                ? t('Drag to turn · fixed height and scale')
                : tool === 'orbit'
                  ? t('Drag to rotate and tilt · scroll or pinch to zoom')
                  : t('Drag to paint · Rotate tool to turn · scroll or pinch to zoom')}
            </div>
            {doc.message && (
              <div className="skin-toast" role="status" aria-label={t('Designer status')}>
                {doc.message}
                <button aria-label={t('Dismiss notification')} onClick={() => doc.setMessage('')}>
                  {t('×')}
                </button>
              </div>
            )}
          </section>
        </>
      )}
      {doc.conflict && !library && dismissedConflict === d.skinId && (
        <button className="skin-conflict-notice" onClick={() => setDismissedConflict(null)}>
          {t('Review account changes to resume saving')}
        </button>
      )}
      {doc.conflict && !library && dismissedConflict !== d.skinId && (
        <StudioDialog
          title={t('Account changes')}
          onClose={() => setDismissedConflict(d.skinId ?? null)}
        >
          <p>{t('This skin changed on another device. Your work is safe in this browser.')}</p>
          {doc.message && <p role="status">{doc.message}</p>}
          <button
            disabled={disabled}
            onClick={() => {
              void request<SkinCollection>('GET', '/api/v1/skins/collection')
                .then((c) => {
                  const design = c.designs.find((s) => s.skinId === d.skinId);
                  if (design) return doc.openDesign(design, true);
                  doc.setMessage(
                    'This design is no longer available. Keep your changes as a new skin.',
                  );
                })
                .catch((e) => doc.setMessage(e instanceof Error ? e : 'Could not load changes.'));
            }}
          >
            {t('Load account changes')}
          </button>
          <button disabled={disabled} onClick={() => void doc.keepAsCopy().then(catalog.reload)}>
            {t('Keep mine as a new skin')}
          </button>
        </StudioDialog>
      )}
      {dialog === 'delete' && deleting && (
        <StudioDialog title={t('Delete skin?')} onClose={() => setDialog(null)}>
          <p>
            <RichMessage
              source={'Delete {slot0}?{slot1}'}
              slots={{
                slot0: deleting.name,
                slot1:
                  collection?.activeSkinId === deleting.skinId
                    ? t(' Your next match will use the default colony.')
                    : '',
              }}
            />
          </p>
          <button disabled={disabled} onClick={() => setDialog(null)}>
            {t('Cancel')}
          </button>
          <button disabled={disabled} onClick={() => void deleteSkin()}>
            {t('Delete skin')}
          </button>
        </StudioDialog>
      )}
      {dialog === 'patterns' && (
        <PatternDialog
          data={d}
          model={model}
          camera={camera}
          action={action}
          phase={phase}
          mode={mode}
          color={brush}
          material={material}
          onClose={() => setDialog(null)}
          onApply={(candidate) => {
            doc.replace(candidate);
            setDialog(null);
          }}
        />
      )}
      {dialog === 'shop' && (
        <StudioDialog title={t('Skin shop')} onClose={() => setDialog(null)}>
          <SkinStore
            onChange={catalog.reload}
            beforeCheckout={async () => {
              try {
                await doc.flush();
              } catch {
                /* Browser recovery survives checkout. */
              }
              return doc.saveLocal();
            }}
          />
        </StudioDialog>
      )}
      {dialog === 'shapes' && (
        <StudioDialog title={t('Swarm shape')} onClose={() => setDialog(null)}>
          <div className="skin-shape-grid">
            {SWARM_MESHES.map((shape) => (
              <button
                key={shape}
                aria-pressed={d.swarmMesh === shape}
                onClick={() => selectShape(shape)}
              >
                <img src={`/skins/thumbs/${swarmModel(shape)}.png`} alt="" />
                <strong>{SWARM_SHAPES[shape].name}</strong>
                <small>{SWARM_SHAPES[shape].description}</small>
              </button>
            ))}
          </div>
          <p className="skin-muted">
            {t('Each shape shares the swarm paint area. Check the result after switching.')}
          </p>
        </StudioDialog>
      )}
      {dialog === 'copy' && (
        <CopyPaintDialog
          data={d}
          model={model}
          onClose={() => setDialog(null)}
          onApply={(data) => {
            doc.replace(data);
            setDialog(null);
          }}
        />
      )}
    </div>
  );
}
