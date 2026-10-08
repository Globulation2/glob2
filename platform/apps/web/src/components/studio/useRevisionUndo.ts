import { useEffect } from 'react';
import { useStudioValue } from './storage.ts';

/** Undo is a new optimistic write, bound to the delivered revision. It never removes history. */
export function useRevisionUndo<T extends { revision: string | number }>(
  key: string,
  draft: T | undefined,
  revisions: { requestId: string; applied: boolean }[],
) {
  const [undo, setUndo] = useStudioValue<{
    before: T;
    seen: string[];
    delivered?: string | number;
  } | null>(key, null);
  useEffect(() => {
    if (!undo || undo.delivered !== undefined || !draft || draft.revision === undo.before.revision)
      return;
    if (revisions.some((r) => r.applied && !undo.seen.includes(r.requestId))) {
      queueMicrotask(() => setUndo({ ...undo, delivered: draft.revision }));
    }
  }, [draft, revisions, undo, setUndo]);
  return {
    remember: () => {
      if (draft) setUndo({ before: draft, seen: revisions.map((r) => r.requestId) });
    },
    before: undo?.before,
    delivered: undo?.delivered,
    canUndo: !!draft && undo?.delivered !== undefined && draft.revision === undo.delivered,
    clear: () => setUndo(null),
  };
}
