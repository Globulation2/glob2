import { useEffect, useRef } from 'react';
import { useStudioValue } from './storage.ts';

/** Immutable map/music versions are edited through parent IDs, rather than mutable draft PUTs. */
export function useVersionApplication<T extends { id: string; status: string; kind: string }>(
  key: string,
  versions: T[] | undefined,
  parent: string | undefined,
  apply: (version: T | undefined) => void,
  restoreParent?: (id: string | undefined) => void,
) {
  const [savedSeen, setSavedSeen] = useStudioValue<string[] | null>(`${key}:seen`, null);
  const seen = useRef<Set<string> | null>(savedSeen ? new Set(savedSeen) : null);
  const [undo, setUndo] = useStudioValue<{ version: string; parent?: string } | null>(key, null);
  const callback = useRef(apply);
  const restore = useRef(restoreParent);
  useEffect(() => {
    callback.current = apply;
    restore.current = restoreParent;
  }, [apply, restoreParent]);
  useEffect(() => {
    if (!versions) return;
    const ready = versions.filter((v) => v.kind === 'generate' && v.status === 'ready');
    if (!seen.current) {
      seen.current = new Set(ready.map((v) => v.id));
      const baseline = [...seen.current];
      queueMicrotask(() => setSavedSeen(baseline));
      return;
    }
    const latest = ready.at(-1);
    const isNew = latest && !seen.current.has(latest.id);
    const previousCount = seen.current.size;
    ready.forEach((v) => seen.current?.add(v.id));
    if (seen.current.size !== previousCount) {
      const delivered = [...seen.current];
      queueMicrotask(() => setSavedSeen(delivered));
    }
    if (!latest || !isNew) return;
    queueMicrotask(() => {
      setUndo({ version: latest.id, parent });
      callback.current(latest);
    });
  }, [versions, parent, setUndo, setSavedSeen]);
  return {
    version: undo?.version,
    canUndo:
      !!undo &&
      parent === undo.version &&
      (!undo.parent || !!restoreParent || !!versions?.some((v) => v.id === undo.parent)),
    undo: () => {
      if (!undo || parent !== undo.version) return;
      const previous = versions?.find((v) => v.id === undo.parent);
      if (undo.parent && !previous) {
        // A persisted target may be outside the currently loaded history page.
        // Restore its ID exactly instead of silently starting a fresh creation.
        if (!restore.current) return;
        restore.current(undo.parent);
      } else callback.current(previous);
      setUndo(null);
    },
  };
}
