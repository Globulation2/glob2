import { SkinStore } from '../skins/Store.tsx';
import { useCallback, useEffect, useRef, useState } from 'react';
import type { ColonySkinVersion, SkinDraft } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';
import { MeshPreview } from '../skins/MeshPreview.tsx';

function contextOf(canvas: HTMLCanvasElement) {
  const context = canvas.getContext('2d');
  if (!context) throw new Error('Painting is unavailable in this browser.');
  return context;
}

type Skin = ColonySkinVersion & { name: string; kind: 'custom' | 'preset'; entitlement: string };
type Catalog = {
  items: Skin[];
  equippedVersionId: string | null;
  equippedBuildingColor: number | null;
};
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
  const [canvas, setCanvas] = useState<HTMLCanvasElement | null>(null);
  const ref = useCallback((node: HTMLCanvasElement | null) => setCanvas(node), []);
  const [name, setName] = useState('My colony');
  const [brush, setBrush] = useState('#ed9252');
  const [building, setBuilding] = useState('#ed9252');
  const [size, setSize] = useState(12);
  const [erase, setErase] = useState(false);
  const [message, setMessage] = useState('');
  const [busy, setBusy] = useState(false);
  const [skinId, setSkinId] = useState<string>();
  const undo = useRef<ImageData[]>([]),
    redo = useRef<ImageData[]>([]);
  const [historyCounts, setHistoryCounts] = useState([0, 0]);
  const catalog = useLoad(
    async (signal) =>
      account
        ? request<Catalog>('GET', '/api/v1/skins', { signal })
        : { items: [], equippedVersionId: null, equippedBuildingColor: null },
    [account?.id],
  );
  useEffect(() => {
    if (!canvas) return;
    const context = contextOf(canvas);
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, 256, 256);
  }, [canvas]);
  function checkpoint() {
    if (!canvas) return;
    undo.current.push(contextOf(canvas).getImageData(0, 0, 256, 256));
    if (undo.current.length > 40) undo.current.shift();
    redo.current = [];
    setHistoryCounts([undo.current.length, redo.current.length]);
  }
  function paint(u: number, v: number) {
    if (!canvas) return;
    const ctx = contextOf(canvas);
    ctx.fillStyle = erase ? '#ffffff' : brush;
    ctx.beginPath();
    ctx.arc(u * 256, v * 256, size / 2, 0, Math.PI * 2);
    ctx.fill();
  }
  function history(back: boolean) {
    if (!canvas) return;
    const from = back ? undo.current : redo.current,
      to = back ? redo.current : undo.current;
    const image = from.pop();
    if (!image) return;
    const ctx = contextOf(canvas);
    to.push(ctx.getImageData(0, 0, 256, 256));
    ctx.putImageData(image, 0, 0);
    setHistoryCounts([undo.current.length, redo.current.length]);
  }
  function pattern(mode: 'fill' | 'stripes' | 'spots') {
    if (!canvas) return;
    checkpoint();
    const ctx = contextOf(canvas);
    ctx.fillStyle = mode === 'fill' ? brush : '#ffffff';
    ctx.fillRect(0, 0, 256, 256);
    ctx.fillStyle = brush;
    if (mode === 'stripes') for (let x = 0; x < 256; x += 32) ctx.fillRect(x, 0, 16, 256);
    if (mode === 'spots')
      for (let y = 16; y < 256; y += 32)
        for (let x = 16; x < 256; x += 32) {
          ctx.beginPath();
          ctx.arc(x, y, 9, 0, Math.PI * 2);
          ctx.fill();
        }
  }
  function saveDraft() {
    if (!canvas) return;
    try {
      localStorage.setItem(
        `glob2-skin-draft:${account?.id ?? 'local'}`,
        JSON.stringify({ name, building, skinId, image: canvas.toDataURL('image/png') }),
      );
      setMessage('Draft saved on this device.');
    } catch {
      setMessage('This browser could not save the draft.');
    }
  }
  async function loadDraft() {
    if (!canvas) return;
    setBusy(true);
    try {
      const raw = localStorage.getItem(`glob2-skin-draft:${account?.id ?? 'local'}`);
      if (!raw) {
        setMessage('No saved draft on this device.');
        return;
      }
      const draft = JSON.parse(raw) as {
        name: string;
        building: string;
        image: string;
        skinId?: string;
      };
      if (
        typeof draft.image !== 'string' ||
        !draft.image.startsWith('data:image/png;base64,') ||
        draft.image.length > 350000 ||
        !/^#[0-9a-f]{6}$/i.test(draft.building) ||
        typeof draft.name !== 'string' ||
        (draft.skinId !== undefined &&
          (typeof draft.skinId !== 'string' ||
            !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(draft.skinId)))
      )
        throw new Error('Invalid draft');
      const image = new Image();
      image.src = draft.image;
      await image.decode();
      if (!alive.current) return;
      if (image.width !== 256 || image.height !== 256) throw new Error('Invalid draft dimensions');
      checkpoint();
      contextOf(canvas).drawImage(image, 0, 0);
      setName(draft.name.slice(0, 64));
      setBuilding(draft.building);
      setSkinId(draft.skinId);
      setMessage('Draft restored.');
    } catch {
      if (alive.current) setMessage('The saved draft could not be restored.');
    } finally {
      if (alive.current) setBusy(false);
    }
  }
  async function accountDraft(save: boolean) {
    if (!canvas) return;
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
            imageBase64: canvas.toDataURL('image/png').split(',')[1],
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
        const image = new Image();
        image.src = `data:image/png;base64,${draft.imageBase64}`;
        await image.decode();
        if (!alive.current) return;
        if (image.width !== 256 || image.height !== 256)
          throw new Error('Invalid draft dimensions.');
        checkpoint();
        contextOf(canvas).drawImage(image, 0, 0);
        setName(draft.name);
        setBuilding(`#${draft.buildingColor.toString(16).padStart(6, '0')}`);
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
    if (!canvas) return;
    setBusy(true);
    setMessage('');
    try {
      const image = new Image();
      image.src = `/api/v1/skins/versions/${skin.id}/texture`;
      await image.decode();
      if (!alive.current) return;
      if (image.width !== 256 || image.height !== 256) throw new Error('Invalid skin dimensions.');
      checkpoint();
      contextOf(canvas).drawImage(image, 0, 0);
      setName(skin.kind === 'custom' ? skin.name : `${skin.name} remix`);
      setBuilding(`#${skin.buildingColor.toString(16).padStart(6, '0')}`);
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
    if (!canvas) return;
    setBusy(true);
    setMessage('');
    try {
      const version = await request<ColonySkinVersion>('POST', '/api/v1/skins/publish', {
        body: {
          name,
          ...(skinId ? { skinId } : {}),
          buildingColor: parseInt(building.slice(1), 16),
          imageBase64: canvas.toDataURL('image/png').split(',')[1],
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
        Paint your units and swarm. Choose a color for the rest of your buildings. Try the designer
        for free; publishing requires the designer unlock.
      </p>
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
            <div>
              <label>
                Paint{' '}
                <input type="color" value={brush} onChange={(e) => setBrush(e.target.value)} />
              </label>
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
                  checked={erase}
                  onChange={(e) => setErase(e.target.checked)}
                />{' '}
                Erase to white
              </label>
            </div>
            <canvas
              ref={ref}
              width={256}
              height={256}
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
            </div>
            <label>
              Building color{' '}
              <input type="color" value={building} onChange={(e) => setBuilding(e.target.value)} />
            </label>
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
          texture={canvas}
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
                {skin.kind === 'preset' ? 'Premade skin' : 'Your design'}{' '}
                <span
                  aria-label="Building color"
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
