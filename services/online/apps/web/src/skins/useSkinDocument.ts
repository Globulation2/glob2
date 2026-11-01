import { MessageError, translateError } from '../messages.ts';
import { message as sourceMessage } from '../messages.ts';
import { t } from '../messages.ts';
/* Canvas contexts are checked by paintCanvas; dimensions are fixed. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import type { ColonySkinVersion, SkinDesign, SwarmMeshId } from '@glob2/protocol';
import { ApiError, request } from '../api.ts';
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
  if (!ctx) throw new MessageError('Painting is unavailable in this browser.');
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
    throw new MessageError('Invalid skin dimensions.');
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
    [status, setStatus] = useState(sourceMessage('Opening studio…')),
    [hydrated, setHydrated] = useState(false);
  const [busy, setBusy] = useState(false),
    [message, setMessage] = useState<string | Error>('');
  const draftRevision = useRef<string | null>(null),
    alive = useRef(true),
    documentGeneration = useRef(0),
    operationPending = useRef(false);
  function requireUnchanged(generation: number) {
    if (documentGeneration.current !== generation)
      throw new MessageError(
        'Your draft changed while the design was loading. Your changes were kept; try opening the design again.',
      );
  }
  const dirty = useRef(false),
    activated = useRef(!accountId),
    conflicted = useRef(false);
  const saving = useRef<Promise<void> | null>(null);
  const creation = useRef<{ id: string; name: string; sourceSkinId?: string } | null>(null);
  const [conflict, setConflict] = useState(false);
  const storageKey = `${KEY}:${accountId ?? 'local'}`;
  function refresh() {
    documentGeneration.current++;
    dirty.current = true;
    setStatus(accountId ? 'Saving…' : sourceMessage('Sign in to save'));
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
          dirty: dirty.current,
          image: canvas.toDataURL('image/png'),
          material: encodeMaterials(d.materials),
        }),
      );
      if (d.skinId)
        localStorage.setItem(
          `${storageKey}:${d.skinId}`,
          localStorage.getItem(manual ? storageKey : `${storageKey}:recovery`)!,
        );
      return true;
    } catch {
      setMessage(
        sourceMessage(
          'Browser recovery is unavailable. Keep this page open until changes are saved.',
        ),
      );
      return false;
    }
  }
  async function restoreLocal(initial = false, isCurrent = () => alive.current) {
    const generation = documentGeneration.current;
    const raw = initial
      ? (localStorage.getItem(`${storageKey}:recovery`) ??
        localStorage.getItem(storageKey) ??
        (accountId ? localStorage.getItem(`${KEY}:local:recovery`) : null))
      : localStorage.getItem(storageKey);
    if (!raw) return;
    const d = JSON.parse(raw) as {
      name: string;
      building: string;
      swarmMesh?: string;
      swarmViewAngle?: number;
      skinId?: string;
      draftRevision?: string | null;
      dirty?: boolean;
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
      throw new MessageError('The saved draft could not be restored.');
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
      dirty.current = d.dirty ?? true;
    } else replace(data);
  }
  useEffect(() => {
    alive.current = true;
    let cancelled = false;
    void Promise.resolve()
      .then(() => restoreLocal(true, () => !cancelled && alive.current))
      .then(() => {
        if (!cancelled)
          setStatus(accountId ? sourceMessage('Choose a skin') : sourceMessage('Sign in to save'));
      })
      .catch((e: unknown) => {
        if (!cancelled) {
          setMessage(e instanceof Error ? e : sourceMessage('Draft recovery failed.'));
          setStatus(sourceMessage('Draft recovery failed'));
        }
      })
      .finally(() => {
        if (!cancelled) {
          setHydrated(true);
          saveLocal();
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
    const timer = window.setTimeout(() => {
      saveLocal();
      if (accountId && activated.current && dirty.current && !conflicted.current)
        void flush().catch(() => undefined);
    }, 1500);
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
  useEffect(() => {
    // Sidebar navigation unmounts the studio before the autosave timer may fire.
    return () => {
      if (hydrated) saveLocal();
    };
    // saveLocal reads the current document through refs.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [hydrated]);
  async function run(operation: () => Promise<void>) {
    if (operationPending.current) return;
    operationPending.current = true;
    setBusy(true);
    setMessage('');
    try {
      await operation();
      return true;
    } catch (e) {
      if (e instanceof ApiError && e.status === 409) {
        conflicted.current = true;
        setConflict(true);
        setStatus(sourceMessage('Account changes need review'));
      }
      if (alive.current)
        setMessage(e instanceof Error ? e : sourceMessage('The operation could not finish.'));
      return false;
    } finally {
      operationPending.current = false;
      if (alive.current) setBusy(false);
    }
  }
  function payload() {
    const d = current.current;
    return {
      revision: draftRevision.current,
      name: d.name.trim() || 'Untitled skin',
      buildingColor: parseInt(d.building.slice(1), 16),
      swarmMesh: d.swarmMesh,
      swarmViewAngle: d.swarmViewAngle,
      ...encoded(),
    };
  }
  async function flush(): Promise<void> {
    finish();
    if (!accountId || !activated.current) {
      saveLocal();
      return;
    }
    if (conflicted.current) throw new MessageError('Resolve the account changes before saving.');
    if (saving.current) {
      await saving.current;
      return flush();
    }
    if (!dirty.current) return;
    const id = current.current.skinId;
    if (!id) return;
    const generation = documentGeneration.current;
    const submitted = payload();
    setStatus('Saving…');
    const operation = (async () => {
      try {
        const result = await request<{ revision: string }>('PUT', `/api/v1/skins/designs/${id}`, {
          body: submitted,
        });
        draftRevision.current = result.revision;
        dirty.current = documentGeneration.current !== generation;
        setStatus(dirty.current ? 'Saving…' : sourceMessage('Saved'));
        saveLocal();
      } catch (e) {
        if (e instanceof ApiError && e.status === 409) {
          conflicted.current = true;
          setConflict(true);
          setStatus(sourceMessage('Account changes need review'));
        } else {
          setStatus(
            navigator.onLine
              ? sourceMessage('Could not save · retrying')
              : sourceMessage('Offline · changes pending'),
          );
        }
        saveLocal();
        throw e;
      }
    })();
    saving.current = operation;
    try {
      await operation;
    } finally {
      saving.current = null;
    }
    if (dirty.current) await flush();
  }
  useEffect(() => {
    const retry = () => {
      if (accountId && activated.current && dirty.current && !conflicted.current)
        void flush().catch(() => undefined);
    };
    window.addEventListener('online', retry);
    const timer = window.setInterval(retry, 10000);
    return () => {
      window.removeEventListener('online', retry);
      window.clearInterval(timer);
    };
    // Reads the latest document and serialized save through refs.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [accountId]);
  async function loadDesign(design: SkinDesign, allowRecovery = true) {
    const generation = documentGeneration.current;
    const [colour, materials] = await Promise.all([
      readColour(`data:image/webp;base64,${design.imageBase64}`),
      decodeMaterials(`data:image/webp;base64,${design.materialBase64}`),
    ]);
    if (!alive.current) return;
    requireUnchanged(generation);
    current.current = {
      name: design.name,
      building: `#${design.buildingColor.toString(16).padStart(6, '0')}`,
      swarmMesh: design.swarmMesh,
      swarmViewAngle: design.swarmViewAngle ?? 0,
      skinId: design.skinId,
      colour,
      materials,
    };
    undo.current = [];
    redo.current = [];
    pending.current = null;
    draftRevision.current = design.revision;
    conflicted.current = false;
    setConflict(false);
    refresh();
    dirty.current = false;
    activated.current = true;
    setStatus(sourceMessage('Saved'));
    if (allowRecovery) {
      const raw = localStorage.getItem(`${storageKey}:${design.skinId}`);
      if (raw) {
        const recovery = JSON.parse(raw) as {
          dirty?: boolean;
          draftRevision?: string;
          image: string;
          material: string;
        } & SkinData;
        if (recovery.dirty) {
          const [localColour, localMaterials] = await Promise.all([
            readColour(recovery.image),
            decodeMaterials(recovery.material),
          ]);
          current.current = {
            ...current.current,
            name: recovery.name,
            building: recovery.building,
            swarmMesh: recovery.swarmMesh,
            swarmViewAngle: recovery.swarmViewAngle,
            colour: localColour,
            materials: localMaterials,
          };
          refresh();
          draftRevision.current = recovery.draftRevision ?? null;
          if (recovery.draftRevision !== design.revision) {
            conflicted.current = true;
            setConflict(true);
            setStatus(sourceMessage('Account changes need review'));
          }
        }
      }
    }
    saveLocal();
  }
  async function openDesign(design: SkinDesign, discardRecovery = false) {
    return run(async () => {
      if (saving.current) await saving.current.catch(() => undefined);
      saveLocal();
      await loadDesign(design, !discardRecovery);
    });
  }
  async function newDesign(name = t('My colony'), sourceSkinId?: string, keepCurrent = false) {
    let result: SkinDesign | undefined;
    await run(async () => {
      if (saving.current) await saving.current.catch(() => undefined);
      saveLocal();
      const working = keepCurrent ? cloneSkin(current.current) : null;
      const { design } = await request<{ design: SkinDesign }>('POST', '/api/v1/skins/designs', {
        body:
          creation.current?.name === name && creation.current.sourceSkinId === sourceSkinId
            ? creation.current
            : (creation.current = {
                id: crypto.randomUUID(),
                name,
                ...(sourceSkinId ? { sourceSkinId } : {}),
              }),
      });
      creation.current = null;
      await loadDesign(design, false);
      if (working) {
        replace({ ...working, skinId: design.skinId });
        await flush();
        localStorage.removeItem(`${KEY}:local:recovery`);
      }
      result = design;
    });
    return result;
  }
  async function keepAsCopy() {
    await run(async () => {
      const local = cloneSkin(current.current);
      const { design } = await request<{ design: SkinDesign }>('POST', '/api/v1/skins/designs', {
        body: { id: crypto.randomUUID(), name: `${local.name.slice(0, 59)} copy` },
      });
      draftRevision.current = design.revision;
      current.current = { ...local, skinId: design.skinId, name: design.name };
      conflicted.current = false;
      setConflict(false);
      activated.current = true;
      refresh();
      await flush();
      if (local.skinId) localStorage.removeItem(`${storageKey}:${local.skinId}`);
      setMessage(sourceMessage('Your changes were saved as a new skin.'));
    });
  }
  async function useInGame(onApplied: () => void) {
    return run(async () => {
      await flush();
      const id = current.current.skinId;
      if (!id) return;
      await request('POST', `/api/v1/skins/designs/${id}/use`, {
        body: { revision: draftRevision.current },
      });
      setMessage(sourceMessage('Used for your next match.'));
      onApplied();
    });
  }
  function forgetDesign(id: string) {
    try {
      localStorage.removeItem(`${storageKey}:${id}`);
      if (current.current.skinId === id) localStorage.removeItem(`${storageKey}:recovery`);
    } catch {
      /* Account deletion succeeded even if browser storage is unavailable. */
    }
    if (current.current.skinId === id) {
      current.current = cloneSkin(initial);
      undo.current = [];
      redo.current = [];
      pending.current = null;
      activated.current = false;
      conflicted.current = false;
      setConflict(false);
      draftRevision.current = null;
      refresh();
      dirty.current = false;
      setStatus(sourceMessage('Choose a skin'));
    }
  }
  return {
    forgetDesign,
    data,
    canvas,
    revision,
    status,
    hydrated,
    busy,
    message: typeof message === 'string' ? t(message) : translateError(message),
    conflict,
    setMessage,
    paint,
    finish,
    replace,
    edit,
    history,
    canUndo: (counts[0] ?? 0) > 0,
    canRedo: (counts[1] ?? 0) > 0,
    saveLocal: () => saveLocal(),
    flush,
    openDesign,
    newDesign,
    keepAsCopy,
    useInGame,
    hasChanges: dirty.current,
    savedRevision: draftRevision.current,
  };
}
