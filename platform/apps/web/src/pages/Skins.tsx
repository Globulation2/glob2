import { SkinStore } from '../skins/Store.tsx';
import { useCallback, useEffect, useRef, useState } from 'react';
import {
  SWARM_MESHES,
  type ColonySkinVersion,
  type SkinDraft,
  type SwarmMeshId,
} from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';
import { MeshPreview } from '../skins/MeshPreview.tsx';
import { SWARM_SHAPES, isSwarmMesh } from '../skins/swarmShapes.ts';
import {
  ATLAS_SIZE,
  MATERIALS,
  MODEL_SIZE,
  MODELS,
  decodeMaterials,
  encodeMaterials,
  paintMaterial,
  type Model,
} from '../skins/atlas.ts';

function contextOf(canvas: HTMLCanvasElement) {
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) throw new Error('Painting is unavailable in this browser.');
  return context;
}

type Skin = ColonySkinVersion & { name: string; kind: 'custom' | 'preset'; entitlement: string };
type Catalog = {
  items: Skin[];
  equippedVersionId: string | null;
  equippedBuildingColor: number | null;
};
type Snapshot = { colour: ImageData; materials: Uint8Array };
const DRAFT_KEY = 'glob2-skin-draft-v2';

async function loadImage(src: string) {
  const image = new Image();
  image.src = src;
  await image.decode();
  if (image.width !== ATLAS_SIZE || image.height !== ATLAS_SIZE)
    throw new Error('Invalid skin dimensions.');
  return image;
}

export function Skins() {
  const { account } = useSession();
  // Remount all canvas/history/async state when the signed-in identity changes.
  return <SkinDesigner key={account?.id ?? 'local'} />;
}
function SkinDesigner() {
  const { account } = useSession();
  const alive = useRef(true);
  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
    };
  }, []);
  const [draftRevision, setDraftRevision] = useState<string | null>(null);
  // The whole colour atlas lives off screen; the visible canvas shows one model.
  const [atlas] = useState(() => {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = ATLAS_SIZE;
    const context = contextOf(canvas);
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, ATLAS_SIZE, ATLAS_SIZE);
    return canvas;
  });
  const materials = useRef<Uint8Array>(new Uint8Array(ATLAS_SIZE * ATLAS_SIZE));
  const [materialRevision, setMaterialRevision] = useState(0);
  const [view, setView] = useState<HTMLCanvasElement | null>(null);
  const ref = useCallback((node: HTMLCanvasElement | null) => setView(node), []);
  const [model, setModel] = useState<Model>(MODELS[0]);
  const [mode, setMode] = useState<'colour' | 'material'>('colour');
  const [material, setMaterial] = useState(2);
  const [showMaterials, setShowMaterials] = useState(false);
  const [name, setName] = useState('My colony');
  const [brush, setBrush] = useState('#ed9252');
  const [building, setBuilding] = useState('#ed9252');
  const [swarmMesh, setSwarmMesh] = useState<SwarmMeshId>('classic');
  const [size, setSize] = useState(12);
  const [erase, setErase] = useState(false);
  const [message, setMessage] = useState('');
  const [busy, setBusy] = useState(false);
  const [skinId, setSkinId] = useState<string>();
  const undo = useRef<Snapshot[]>([]),
    redo = useRef<Snapshot[]>([]);
  const [historyCounts, setHistoryCounts] = useState([0, 0]);
  const catalog = useLoad(
    async (signal) =>
      account
        ? request<Catalog>('GET', '/api/v1/skins', { signal })
        : { items: [], equippedVersionId: null, equippedBuildingColor: null },
    [account?.id],
  );
  const overlay = showMaterials || mode === 'material';
  const redraw = useCallback(() => {
    if (!view) return;
    const ctx = contextOf(view);
    ctx.drawImage(atlas, model.x, model.y, MODEL_SIZE, MODEL_SIZE, 0, 0, MODEL_SIZE, MODEL_SIZE);
    if (!overlay) return;
    for (const { id, swatch } of MATERIALS.slice(1)) {
      ctx.fillStyle = swatch;
      for (let y = 0; y < MODEL_SIZE; y++) {
        let start = -1;
        for (let x = 0; x <= MODEL_SIZE; x++) {
          const on =
            x < MODEL_SIZE && materials.current[(model.y + y) * ATLAS_SIZE + model.x + x] === id;
          if (on && start < 0) start = x;
          if (!on && start >= 0) {
            ctx.fillRect(start, y, x - start, 1);
            start = -1;
          }
        }
      }
    }
  }, [view, atlas, model, overlay]);
  useEffect(redraw, [redraw, materialRevision]);
  const materialsChanged = () => setMaterialRevision((r) => r + 1);
  const readMaterials = useCallback(() => materials.current, []);
  function snapshot(): Snapshot {
    return {
      colour: contextOf(atlas).getImageData(0, 0, ATLAS_SIZE, ATLAS_SIZE),
      materials: materials.current.slice(),
    };
  }
  function restore(state: Snapshot) {
    contextOf(atlas).putImageData(state.colour, 0, 0);
    materials.current = state.materials;
    materialsChanged();
  }
  function checkpoint() {
    undo.current.push(snapshot());
    if (undo.current.length > 30) undo.current.shift();
    redo.current = [];
    setHistoryCounts([undo.current.length, redo.current.length]);
  }
  // u, v address the active model's 256px quadrant (also its mesh UVs).
  function paint(u: number, v: number) {
    const x = model.x + u * MODEL_SIZE,
      y = model.y + v * MODEL_SIZE;
    if (mode === 'material') {
      paintMaterial(materials.current, model, x, y, size / 2, material);
      materialsChanged();
      return;
    }
    const ctx = contextOf(atlas);
    ctx.save();
    ctx.beginPath();
    ctx.rect(model.x, model.y, MODEL_SIZE, MODEL_SIZE);
    ctx.clip();
    ctx.fillStyle = erase ? '#ffffff' : brush;
    ctx.beginPath();
    ctx.arc(x, y, size / 2, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    redraw();
  }
  function history(back: boolean) {
    const from = back ? undo.current : redo.current,
      to = back ? redo.current : undo.current;
    const state = from.pop();
    if (!state) return;
    to.push(snapshot());
    restore(state);
    setHistoryCounts([undo.current.length, redo.current.length]);
  }
  function inPattern(kind: 'fill' | 'stripes' | 'spots', x: number, y: number) {
    if (kind === 'stripes') return x % 32 < 16;
    if (kind === 'spots') {
      const dx = (x % 32) - 16,
        dy = (y % 32) - 16;
      return dx * dx + dy * dy < 81;
    }
    return true;
  }
  function pattern(kind: 'fill' | 'stripes' | 'spots') {
    checkpoint();
    if (mode === 'material') {
      for (let y = 0; y < MODEL_SIZE; y++)
        for (let x = 0; x < MODEL_SIZE; x++)
          materials.current[(model.y + y) * ATLAS_SIZE + model.x + x] = inPattern(kind, x, y)
            ? material
            : 0;
      materialsChanged();
      return;
    }
    const ctx = contextOf(atlas);
    ctx.fillStyle = kind === 'fill' ? brush : '#ffffff';
    ctx.fillRect(model.x, model.y, MODEL_SIZE, MODEL_SIZE);
    ctx.fillStyle = brush;
    if (kind === 'stripes')
      for (let x = 0; x < MODEL_SIZE; x += 32) ctx.fillRect(model.x + x, model.y, 16, MODEL_SIZE);
    if (kind === 'spots')
      for (let y = 16; y < MODEL_SIZE; y += 32)
        for (let x = 16; x < MODEL_SIZE; x += 32) {
          ctx.beginPath();
          ctx.arc(model.x + x, model.y + y, 9, 0, Math.PI * 2);
          ctx.fill();
        }
    redraw();
  }
  function copyToAll() {
    checkpoint();
    const ctx = contextOf(atlas);
    const colour = ctx.getImageData(model.x, model.y, MODEL_SIZE, MODEL_SIZE);
    for (const other of MODELS) {
      if (other.id === model.id) continue;
      ctx.putImageData(colour, other.x, other.y);
      for (let y = 0; y < MODEL_SIZE; y++)
        materials.current.copyWithin(
          (other.y + y) * ATLAS_SIZE + other.x,
          (model.y + y) * ATLAS_SIZE + model.x,
          (model.y + y) * ATLAS_SIZE + model.x + MODEL_SIZE,
        );
    }
    materialsChanged();
    setMessage(`Copied the ${model.name.toLowerCase()} design to every model.`);
  }
  async function install(image: CanvasImageSource, map: Uint8Array) {
    checkpoint();
    contextOf(atlas).drawImage(image, 0, 0);
    materials.current = map;
    materialsChanged();
  }
  const encoded = () => ({
    imageBase64: atlas.toDataURL('image/png').split(',')[1],
    materialBase64: encodeMaterials(materials.current).split(',')[1],
  });
  function saveDraft() {
    try {
      localStorage.setItem(
        `${DRAFT_KEY}:${account?.id ?? 'local'}`,
        JSON.stringify({
          name,
          building,
          swarmMesh,
          skinId,
          image: atlas.toDataURL('image/png'),
          material: encodeMaterials(materials.current),
        }),
      );
      setMessage('Draft saved on this device.');
    } catch {
      setMessage('This browser could not save the draft.');
    }
  }
  async function loadDraft() {
    setBusy(true);
    try {
      const raw = localStorage.getItem(`${DRAFT_KEY}:${account?.id ?? 'local'}`);
      if (!raw) {
        setMessage('No saved draft on this device.');
        return;
      }
      const draft = JSON.parse(raw) as {
        name: string;
        building: string;
        image: string;
        material: string;
        skinId?: string;
        swarmMesh?: string;
      };
      const png = (value: unknown, limit: number) =>
        typeof value === 'string' &&
        value.startsWith('data:image/png;base64,') &&
        value.length <= limit;
      if (
        !png(draft.image, 1500000) ||
        !png(draft.material, 400000) ||
        !/^#[0-9a-f]{6}$/i.test(draft.building) ||
        typeof draft.name !== 'string' ||
        // Drafts saved before shape choice have no swarmMesh and use the classic swarm.
        (draft.swarmMesh !== undefined && !isSwarmMesh(draft.swarmMesh)) ||
        (draft.skinId !== undefined &&
          (typeof draft.skinId !== 'string' ||
            !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(draft.skinId)))
      )
        throw new Error('Invalid draft');
      const [image, map] = await Promise.all([
        loadImage(draft.image),
        decodeMaterials(draft.material),
      ]);
      if (!alive.current) return;
      await install(image, map);
      setName(draft.name.slice(0, 64));
      setBuilding(draft.building);
      setSwarmMesh(isSwarmMesh(draft.swarmMesh) ? draft.swarmMesh : 'classic');
      setSkinId(draft.skinId);
      setMessage('Draft restored.');
    } catch {
      if (alive.current) setMessage('The saved draft could not be restored.');
    } finally {
      if (alive.current) setBusy(false);
    }
  }
  async function accountDraft(save: boolean) {
    setBusy(true);
    setMessage('');
    try {
      if (save) {
        const result = await request<{ revision: string }>('PUT', '/api/v1/skins/draft', {
          body: {
            revision: draftRevision,
            ...(skinId ? { skinId } : {}),
            name,
            buildingColor: parseInt(building.slice(1), 16),
            swarmMesh,
            ...encoded(),
          },
        });
        if (!alive.current) return;
        setDraftRevision(result.revision);
        setMessage('Draft saved to your account.');
      } else {
        const { draft } = await request<{ draft: SkinDraft | null }>('GET', '/api/v1/skins/draft');
        if (!alive.current) return;
        if (!draft) {
          setDraftRevision(null);
          setMessage('No saved draft in your account.');
          return;
        }
        const [image, map] = await Promise.all([
          loadImage(`data:image/png;base64,${draft.imageBase64}`),
          decodeMaterials(`data:image/png;base64,${draft.materialBase64}`),
        ]);
        if (!alive.current) return;
        await install(image, map);
        setName(draft.name);
        setBuilding(`#${draft.buildingColor.toString(16).padStart(6, '0')}`);
        setSwarmMesh(draft.swarmMesh);
        setSkinId(draft.skinId);
        setDraftRevision(draft.revision);
        setMessage('Account draft restored.');
      }
    } catch (e) {
      if (alive.current) setMessage(e instanceof Error ? e.message : 'Could not sync your draft.');
    } finally {
      if (alive.current) setBusy(false);
    }
  }
  async function openDesign(skin: Skin) {
    setBusy(true);
    setMessage('');
    try {
      const [image, map] = await Promise.all([
        loadImage(`/api/v1/skins/versions/${skin.id}/texture`),
        decodeMaterials(`/api/v1/skins/versions/${skin.id}/material`),
      ]);
      if (!alive.current) return;
      await install(image, map);
      setName(skin.kind === 'custom' ? skin.name : `${skin.name} remix`);
      setBuilding(`#${skin.buildingColor.toString(16).padStart(6, '0')}`);
      setSwarmMesh(skin.swarmMesh);
      setSkinId(skin.kind === 'custom' ? skin.skinId : undefined);
      setMessage(
        skin.kind === 'custom'
          ? 'Design opened. Publishing saves a new version; equip it when ready.'
          : 'Preset copied into the designer. Publishing requires the designer unlock.',
      );
    } catch (e) {
      if (alive.current) setMessage(e instanceof Error ? e.message : 'Could not open this design.');
    } finally {
      if (alive.current) setBusy(false);
    }
  }
  async function publish() {
    setBusy(true);
    setMessage('');
    try {
      const version = await request<ColonySkinVersion>('POST', '/api/v1/skins/publish', {
        body: {
          name,
          ...(skinId ? { skinId } : {}),
          buildingColor: parseInt(building.slice(1), 16),
          swarmMesh,
          ...encoded(),
        },
      });
      if (!alive.current) return;
      setSkinId(version.skinId);
      catalog.reload();
      setMessage('Published. Choose Equip below to use this version.');
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Could not publish.');
    } finally {
      setBusy(false);
    }
  }
  async function equip(id: string | null) {
    setBusy(true);
    try {
      await request('PUT', '/api/v1/skins/equipped', {
        body: { versionId: id, buildingColor: parseInt(building.slice(1), 16) },
      });
      catalog.reload();
      setMessage(id ? 'Skin equipped for your next match.' : 'Default colony selected.');
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Could not equip.');
    } finally {
      setBusy(false);
    }
  }
  return (
    <>
      <h1>Colony skins</h1>
      <p>
        Paint each kind of unit and your swarm, choose what each part is made of and your swarm's
        shape, and pick a color for the rest of your buildings. Try the designer for free;
        publishing requires the designer unlock.
      </p>
      <p>
        Glob paint repeats automatically on matching front/back and top/bottom surfaces so their
        flips stay seamless.
      </p>
      <div role="tablist" aria-label="Model to paint" style={{ display: 'flex', gap: 8 }}>
        {MODELS.map((m) => (
          <button
            key={m.id}
            role="tab"
            aria-selected={m.id === model.id}
            onClick={() => setModel(m)}
            style={{ fontWeight: m.id === model.id ? 'bold' : undefined }}
          >
            {m.name}
          </button>
        ))}
      </div>
      <div
        className="skin-designer"
        style={{
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fit,minmax(280px,1fr))',
          gap: '1.5rem',
        }}
      >
        <section aria-label="Paint tools">
          <fieldset disabled={busy} style={{ border: 0, padding: 0, margin: 0 }}>
            <label>
              Skin name{' '}
              <input maxLength={64} value={name} onChange={(e) => setName(e.target.value)} />
            </label>
            <div role="radiogroup" aria-label="Brush paints">
              Brush paints{' '}
              <label className="check">
                <input
                  type="radio"
                  name="brush-mode"
                  checked={mode === 'colour'}
                  onChange={() => setMode('colour')}
                />{' '}
                Colour
              </label>{' '}
              <label className="check">
                <input
                  type="radio"
                  name="brush-mode"
                  checked={mode === 'material'}
                  onChange={() => setMode('material')}
                />{' '}
                Material
              </label>
            </div>
            {mode === 'colour' ? (
              <div>
                <label>
                  Paint{' '}
                  <input type="color" value={brush} onChange={(e) => setBrush(e.target.value)} />
                </label>
                <label>
                  <input
                    type="checkbox"
                    checked={erase}
                    onChange={(e) => setErase(e.target.checked)}
                  />{' '}
                  Erase to white
                </label>
              </div>
            ) : (
              <div role="radiogroup" aria-label="Material">
                {MATERIALS.map((m) => (
                  <label key={m.id} className="check" style={{ marginRight: 8 }}>
                    <input
                      type="radio"
                      name="material"
                      checked={material === m.id}
                      onChange={() => setMaterial(m.id)}
                    />{' '}
                    <span
                      aria-hidden="true"
                      style={{
                        display: 'inline-block',
                        width: 12,
                        height: 12,
                        border: '1px solid #888',
                        background: m.swatch,
                      }}
                    />{' '}
                    {m.name}
                  </label>
                ))}
              </div>
            )}
            <label>
              Brush size{' '}
              <input
                type="range"
                min="1"
                max="64"
                value={size}
                onChange={(e) => setSize(Number(e.target.value))}
              />
            </label>
            <label>
              <input
                type="checkbox"
                checked={overlay}
                disabled={mode === 'material'}
                onChange={(e) => setShowMaterials(e.target.checked)}
              />{' '}
              Show materials
            </label>
            <canvas
              ref={ref}
              width={MODEL_SIZE}
              height={MODEL_SIZE}
              aria-label="Paint texture"
              style={{ width: 256, height: 256, touchAction: 'none', border: '1px solid #888' }}
              onPointerDown={(e) => {
                if (busy) return;
                checkpoint();
                e.currentTarget.setPointerCapture(e.pointerId);
                const r = e.currentTarget.getBoundingClientRect();
                paint((e.clientX - r.left) / r.width, (e.clientY - r.top) / r.height);
              }}
              onPointerMove={(e) => {
                if (e.buttons && !busy) {
                  const r = e.currentTarget.getBoundingClientRect();
                  paint((e.clientX - r.left) / r.width, (e.clientY - r.top) / r.height);
                }
              }}
            />
            <div>
              <button disabled={!historyCounts[0]} onClick={() => history(true)}>
                Undo
              </button>
              <button disabled={!historyCounts[1]} onClick={() => history(false)}>
                Redo
              </button>
              <button onClick={() => pattern('fill')}>Fill</button>
              <button onClick={() => pattern('stripes')}>Try stripes</button>
              <button onClick={() => pattern('spots')}>Try spots</button>
              <button onClick={copyToAll}>Copy to all models</button>
            </div>
            <label>
              Building color{' '}
              <input type="color" value={building} onChange={(e) => setBuilding(e.target.value)} />
            </label>
            <fieldset aria-describedby="swarm-shape-hint">
              <legend>Swarm shape</legend>
              {SWARM_MESHES.map((mesh) => (
                <label key={mesh} style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    type="radio"
                    name="swarm-shape"
                    value={mesh}
                    checked={swarmMesh === mesh}
                    onChange={() => {
                      setSwarmMesh(mesh);
                      // Show the new shape; the swarm quadrant paints every shape.
                      setModel(MODELS[3]);
                    }}
                  />
                  <span>
                    <strong>{SWARM_SHAPES[mesh].name}</strong> {SWARM_SHAPES[mesh].description}
                  </span>
                </label>
              ))}
              <p id="swarm-shape-hint">
                Each shape takes paint in its own way, so check your swarm in the preview after
                switching. Your units keep their paint.
              </p>
            </fieldset>
            <div>
              <button disabled={busy} onClick={saveDraft}>
                Save on this device
              </button>
              <button disabled={busy} onClick={() => void loadDraft()}>
                Restore from this device
              </button>
              {account?.kind === 'registered' && (
                <>
                  <button disabled={busy} onClick={() => void accountDraft(true)}>
                    Save to account
                  </button>
                  <button disabled={busy} onClick={() => void accountDraft(false)}>
                    Restore from account
                  </button>
                </>
              )}
              <button
                disabled={busy || !account || account.kind !== 'registered'}
                onClick={() => void publish()}
              >
                {skinId ? 'Publish new version' : 'Publish skin'}
              </button>
            </div>
            {skinId && (
              <p>
                Editing a published design. Existing versions stay available.{' '}
                <button
                  onClick={() => {
                    setSkinId(undefined);
                    setMessage('This painting will publish as a separate design.');
                  }}
                >
                  Make a separate design
                </button>
              </p>
            )}
            {!account && (
              <p>
                <a href="/signin">Sign in</a> to publish or equip a skin.
              </p>
            )}
          </fieldset>
        </section>
        <MeshPreview
          texture={atlas}
          swarmMesh={swarmMesh}
          materials={readMaterials}
          materialRevision={materialRevision}
          model={model}
          onPaint={(u, v) => {
            if (!busy) paint(u, v);
          }}
          onStroke={() => {
            if (!busy) checkpoint();
          }}
        />
      </div>
      <p role="status" aria-label="Designer status">
        {message}
      </p>
      <SkinStore onChange={catalog.reload} />
      <h2>Your colony looks</h2>
      <button disabled={busy || !account} onClick={() => void equip(null)}>
        Use default colony
      </button>
      {catalog.status === 'error' && <p role="alert">{catalog.error.message}</p>}
      {catalog.status === 'ready' && (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
          {catalog.data.items.map((skin) => (
            <article key={skin.id} data-version-id={skin.id} style={{ maxWidth: 200 }}>
              <img
                width={128}
                height={128}
                src={`/api/v1/skins/versions/${skin.id}/texture`}
                alt={`${skin.name} paint`}
              />
              <h3>{skin.name}</h3>
              <button disabled={busy} onClick={() => void openDesign(skin)}>
                {skin.kind === 'custom' ? 'Edit this version' : 'Use as a starting point'}
              </button>
              <p>
                {skin.kind === 'preset' ? 'Premade skin' : 'Your design'},{' '}
                {SWARM_SHAPES[skin.swarmMesh].name.toLowerCase()} swarm{' '}
                <span
                  role="img"
                  aria-label={`Building color #${skin.buildingColor.toString(16).padStart(6, '0')}`}
                  style={{
                    display: 'inline-block',
                    width: 16,
                    height: 16,
                    background: `#${skin.buildingColor.toString(16).padStart(6, '0')}`,
                  }}
                />
              </p>
              <button
                disabled={
                  busy ||
                  (catalog.data.equippedVersionId === skin.id &&
                    (catalog.data.equippedBuildingColor ?? skin.buildingColor) ===
                      parseInt(building.slice(1), 16))
                }
                onClick={() => void equip(skin.id)}
              >
                {catalog.data.equippedVersionId === skin.id
                  ? (catalog.data.equippedBuildingColor ?? skin.buildingColor) ===
                    parseInt(building.slice(1), 16)
                    ? 'Equipped'
                    : 'Apply building color'
                  : 'Equip'}
              </button>
            </article>
          ))}
        </div>
      )}
    </>
  );
}
