/* Canvas contexts are checked by paintCanvas; dimensions are fixed. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import type { ColonySkinVersion, SkinDraft, SwarmMeshId } from '@glob2/protocol';
import { request } from '../api.ts';
import { ATLAS_SIZE, decodeMaterials, encodeMaterials } from './atlas.ts';
import { isSwarmMesh } from './swarmShapes.ts';
export type SkinData = {
  name: string;
  building: string;
  swarmMesh: SwarmMeshId;
  swarmViewAngle: number;
  skinId?: string;
  colour: Uint8ClampedArray;
  materials: Uint8Array;
};
export type Skin = ColonySkinVersion & {
  name: string;
  kind: 'custom' | 'preset';
  entitlement: string;
};
export type Catalog = {
  items: Skin[];
  equippedVersionId: string | null;
  equippedBuildingColor: number | null;
};
const KEY = 'glob2-skin-draft-v2';
export function cloneSkin(data: SkinData): SkinData {
  return { ...data, colour: data.colour.slice(), materials: data.materials.slice() };
}
export function paintCanvas(colour?: Uint8ClampedArray) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = ATLAS_SIZE;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('Painting is unavailable in this browser.');
  if (colour)
    ctx.putImageData(new ImageData(new Uint8ClampedArray(colour), ATLAS_SIZE, ATLAS_SIZE), 0, 0);
  else {
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, ATLAS_SIZE, ATLAS_SIZE);
  }
  return canvas;
}
async function readColour(src: string) {
  const image = new Image();
  image.src = src;
  await image.decode();
  if (image.width !== ATLAS_SIZE || image.height !== ATLAS_SIZE)
    throw new Error('Invalid skin dimensions.');
  const canvas = paintCanvas();
  const ctx = canvas.getContext('2d')!;
  ctx.drawImage(image, 0, 0);
  return ctx.getImageData(0, 0, ATLAS_SIZE, ATLAS_SIZE).data;
}
export function useSkinDocument(accountId: string | undefined) {
  const [canvas] = useState(() => paintCanvas());
  const [initial] = useState<SkinData>(() => ({
    name: 'My colony',
    building: '#ed9252',
    swarmMesh: 'classic',
    swarmViewAngle: 0,
    colour: new Uint8ClampedArray(ATLAS_SIZE * ATLAS_SIZE * 4).fill(255),
    materials: new Uint8Array(ATLAS_SIZE * ATLAS_SIZE),
  }));
  const current = useRef(initial);
  const [data, setData] = useState(initial);
  const [counts, setCounts] = useState([0, 0]);
  const undo = useRef<SkinData[]>([]),
    redo = useRef<SkinData[]>([]),
    pending = useRef<SkinData | null>(null);
  const [revision, setRevision] = useState(0),
    [status, setStatus] = useState('Opening studio…'),
    [hydrated, setHydrated] = useState(false);
  const [busy, setBusy] = useState(false),
    [message, setMessage] = useState('');
  const draftRevision = useRef<string | null>(null),
    alive = useRef(true),
    documentGeneration = useRef(0),
    operationPending = useRef(false);
  function requireUnchanged(generation: number) {
    if (documentGeneration.current !== generation)
      throw new Error(
        'Your draft changed while the design was loading. Your changes were kept; try opening the design again.',
      );
  }
  const storageKey = `${KEY}:${accountId ?? 'local'}`;
  function refresh() {
    documentGeneration.current++;
    setStatus('Saving…');
    setData({ ...current.current });
    setCounts([undo.current.length, redo.current.length]);
    canvas
      .getContext('2d')!
      .putImageData(
        new ImageData(new Uint8ClampedArray(current.current.colour), ATLAS_SIZE, ATLAS_SIZE),
        0,
        0,
      );
    setRevision((v) => v + 1);
  }
  function begin() {
    pending.current ??= cloneSkin(current.current);
  }
  function finish(cancel = false) {
    if (!pending.current) return;
    if (cancel) current.current = pending.current;
    else {
      undo.current.push(pending.current);
      if (undo.current.length > 30) undo.current.shift();
      redo.current = [];
    }
    pending.current = null;
    refresh();
  }
  function paint(mutate: (data: SkinData) => void) {
    begin();
    mutate(current.current);
    refresh();
  }
  function replace(data: SkinData) {
    begin();
    current.current = cloneSkin(data);
    finish();
  }
  function edit(values: Partial<Omit<SkinData, 'colour' | 'materials'>>) {
    begin();
    current.current = { ...current.current, ...values };
    finish();
  }
  function history(back: boolean) {
    finish();
    const from = back ? undo.current : redo.current,
      to = back ? redo.current : undo.current;
    const data = from.pop();
    if (data) {
      to.push(cloneSkin(current.current));
      current.current = data;
      refresh();
    }
  }
  function encoded() {
    return {
      imageBase64: canvas.toDataURL('image/png').split(',')[1]!,
      materialBase64: encodeMaterials(current.current.materials).split(',')[1]!,
    };
  }
  function saveLocal(manual = false) {
    try {
      const d = current.current;
      localStorage.setItem(
        manual ? storageKey : `${storageKey}:recovery`,
        JSON.stringify({
          name: d.name,
          building: d.building,
          swarmMesh: d.swarmMesh,
          swarmViewAngle: d.swarmViewAngle,
          skinId: d.skinId,
          draftRevision: draftRevision.current,
          image: canvas.toDataURL('image/png'),
          material: encodeMaterials(d.materials),
        }),
      );
      setStatus('Saved on this device');
      return true;
    } catch {
      setStatus('Could not save on this device');
      return false;
    }
  }
  async function restoreLocal(initial = false, isCurrent = () => alive.current) {
    const generation = documentGeneration.current;
    const raw = initial
      ? (localStorage.getItem(`${storageKey}:recovery`) ?? localStorage.getItem(storageKey))
      : localStorage.getItem(storageKey);
    if (!raw) return;
    const d = JSON.parse(raw) as {
      name: string;
      building: string;
      swarmMesh?: string;
      swarmViewAngle?: number;
      skinId?: string;
      draftRevision?: string | null;
      image: string;
      material: string;
    };
    if (
      typeof d.name !== 'string' ||
      !/^#[a-f0-9]{6}$/i.test(d.building) ||
      !d.image?.startsWith('data:image/png;base64,') ||
      d.image.length > 1500000 ||
      !d.material?.startsWith('data:image/png;base64,') ||
      d.material.length > 400000 ||
      (d.swarmMesh !== undefined && !isSwarmMesh(d.swarmMesh)) ||
      (d.swarmViewAngle !== undefined &&
        (!Number.isInteger(d.swarmViewAngle) || d.swarmViewAngle < 0 || d.swarmViewAngle > 359)) ||
      (d.skinId !== undefined && !/^[a-f0-9-]{36}$/.test(d.skinId))
    )
      throw new Error('The saved draft could not be restored.');
    const [colour, materials] = await Promise.all([
      readColour(d.image),
      decodeMaterials(d.material),
    ]);
    if (!isCurrent()) return;
    requireUnchanged(generation);
    const data: SkinData = {
      name: d.name.slice(0, 64),
      building: d.building,
      swarmMesh: isSwarmMesh(d.swarmMesh) ? d.swarmMesh : 'classic',
      swarmViewAngle: d.swarmViewAngle ?? 0,
      ...(d.skinId ? { skinId: d.skinId } : {}),
      colour,
      materials,
    };
    if (initial) {
      // Remember the last account revision known by this account's recovery.
      // The server still rejects a newer revision saved from another device.
      if (typeof d.draftRevision === 'string' && /^[a-f0-9-]{36}$/.test(d.draftRevision))
        draftRevision.current = d.draftRevision;
      current.current = data;
      refresh();
    } else replace(data);
  }
  useEffect(() => {
    alive.current = true;
    let cancelled = false;
    void Promise.resolve()
      .then(() => restoreLocal(true, () => !cancelled && alive.current))
      .then(() => {
        if (!cancelled) setStatus('Ready');
      })
      .catch((e: unknown) => {
        if (!cancelled) {
          setMessage(e instanceof Error ? e.message : 'Draft recovery failed.');
          setStatus('Draft recovery failed');
        }
      })
      .finally(() => {
        if (!cancelled) {
          setHydrated(true);
        }
      });
    return () => {
      cancelled = true;
      alive.current = false;
    };
    // The owner remounts this hook when the account identity changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  useEffect(() => {
    if (!hydrated || !revision) return;
    const timer = window.setTimeout(() => saveLocal(), 700);
    return () => clearTimeout(timer);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [revision, hydrated]);
  useEffect(() => {
    const save = () => {
      if (hydrated && revision) saveLocal();
    };
    window.addEventListener('pagehide', save);
    return () => window.removeEventListener('pagehide', save);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [hydrated, revision]);
  async function run(operation: () => Promise<void>) {
    if (operationPending.current) return;
    operationPending.current = true;
    setBusy(true);
    setMessage('');
    try {
      await operation();
    } catch (e) {
      if (alive.current)
        setMessage(e instanceof Error ? e.message : 'The operation could not finish.');
    } finally {
      operationPending.current = false;
      if (alive.current) setBusy(false);
    }
  }
  async function accountDraft(save: boolean) {
    await run(async () => {
      const generation = documentGeneration.current;
      const d = current.current;
      if (save) {
        const result = await request<{ revision: string }>('PUT', '/api/v1/skins/draft', {
          body: {
            revision: draftRevision.current,
            name: d.name,
            ...(d.skinId ? { skinId: d.skinId } : {}),
            buildingColor: parseInt(d.building.slice(1), 16),
            swarmMesh: d.swarmMesh,
            swarmViewAngle: d.swarmViewAngle,
            ...encoded(),
          },
        });
        if (alive.current) {
          draftRevision.current = result.revision;
          saveLocal();
          setMessage(
            documentGeneration.current === generation
              ? 'Draft saved to your account.'
              : 'Draft version saved to your account. Your newer changes remain on this device.',
          );
        }
      } else {
        const { draft } = await request<{ draft: SkinDraft | null }>('GET', '/api/v1/skins/draft');
        if (!draft) {
          if (alive.current) {
            draftRevision.current = null;
            setMessage('No account draft yet.');
          }
          return;
        }
        const [colour, materials] = await Promise.all([
          readColour(`data:image/png;base64,${draft.imageBase64}`),
          decodeMaterials(`data:image/png;base64,${draft.materialBase64}`),
        ]);
        if (alive.current) {
          requireUnchanged(generation);
          replace({
            name: draft.name,
            building: `#${draft.buildingColor.toString(16).padStart(6, '0')}`,
            swarmMesh: draft.swarmMesh,
            swarmViewAngle: draft.swarmViewAngle ?? 0,
            ...(draft.skinId ? { skinId: draft.skinId } : {}),
            colour,
            materials,
          });
          draftRevision.current = draft.revision;
          setMessage('Account draft restored.');
        }
      }
    });
  }
  async function openDesign(skin: Skin) {
    await run(async () => {
      const generation = documentGeneration.current;
      const [colour, materials] = await Promise.all([
        readColour(`/api/v1/skins/versions/${skin.id}/texture`),
        decodeMaterials(`/api/v1/skins/versions/${skin.id}/material`),
      ]);
      if (alive.current) {
        requireUnchanged(generation);
        replace({
          colour,
          materials,
          name: skin.kind === 'custom' ? skin.name : `${skin.name} remix`,
          building: `#${skin.buildingColor.toString(16).padStart(6, '0')}`,
          swarmMesh: skin.swarmMesh,
          swarmViewAngle: skin.swarmViewAngle ?? 0,
          ...(skin.kind === 'custom' ? { skinId: skin.skinId } : {}),
        });
      }
    });
  }
  async function publish(onPublished: () => void) {
    await run(async () => {
      const generation = documentGeneration.current;
      const d = current.current;
      const version = await request<ColonySkinVersion>('POST', '/api/v1/skins/publish', {
        body: {
          name: d.name,
          ...(d.skinId ? { skinId: d.skinId } : {}),
          buildingColor: parseInt(d.building.slice(1), 16),
          swarmMesh: d.swarmMesh,
          swarmViewAngle: d.swarmViewAngle,
          ...encoded(),
        },
      });
      if (alive.current) {
        if (documentGeneration.current === generation) {
          edit({ skinId: version.skinId });
          setMessage('Published. Open My skins to equip this version.');
        } else {
          setMessage(
            'Published the submitted version. Your newer draft changes were kept; open My skins to view the published version.',
          );
        }
        onPublished();
      }
    });
  }
  return {
    data,
    canvas,
    revision,
    status,
    hydrated,
    busy,
    message,
    setMessage,
    paint,
    finish,
    replace,
    edit,
    history,
    canUndo: (counts[0] ?? 0) > 0,
    canRedo: (counts[1] ?? 0) > 0,
    saveLocal: () => saveLocal(),
    saveCheckpoint: () => saveLocal(true),
    restoreLocal: () => run(() => restoreLocal()),
    accountDraft,
    openDesign,
    publish,
  };
}
