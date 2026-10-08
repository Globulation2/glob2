import { useEffect, useRef } from 'react';
import { useStudioValue } from './storage.ts';

/** Immutable map/music versions are edited through parent IDs, rather than mutable draft PUTs. */
export function useVersionApplication<T extends { id: string; status: string; kind: string }>(
  key: string,
  versions: T[] | undefined,
  parent: string | undefined,
  apply: (version: T | undefined) => void,
) {
  const seen = useRef<Set<string> | null>(null);
  const [undo, setUndo] = useStudioValue<{ version: string; parent?: string } | null>(key, null);
  const callback = useRef(apply);
  useEffect(() => {
    callback.current = apply;
  }, [apply]);
  useEffect(() => {
    if (!versions) return;
    const ready = versions.filter((v) => v.kind === 'generate' && v.status === 'ready');
    if (!seen.current) {
      seen.current = new Set(ready.map((v) => v.id));
      return;
    }
    const latest = ready.at(-1);
    const isNew = latest && !seen.current.has(latest.id);
    ready.forEach((v) => seen.current?.add(v.id));
    if (!latest || !isNew) return;
    queueMicrotask(() => {
      setUndo({ version: latest.id, parent });
      callback.current(latest);
    });
  }, [versions, parent, setUndo]);
  return {
    version: undo?.version,
    canUndo: !!undo && parent === undo.version,
    undo: () => {
      if (!undo || parent !== undo.version) return;
      callback.current(versions?.find((v) => v.id === undo.parent));
      setUndo(null);
    },
  };
}
