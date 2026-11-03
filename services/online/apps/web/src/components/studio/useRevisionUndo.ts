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
  const [pending, setPending] = useStudioValue<{ before: T; seen: string[] } | null>(
    `${key}:pending`,
    null,
  );
  useEffect(() => {
    // Retain a previously delivered Undo while a turn is discussing or failing.
    // Older browser state may have stored the pending baseline in the Undo slot.
    const candidate = pending ?? (undo?.delivered === undefined ? undo : null);
    if (!candidate || !draft || draft.revision === candidate.before.revision) return;
    if (revisions.some((r) => r.applied && !candidate.seen.includes(r.requestId))) {
      queueMicrotask(() => {
        setUndo({ ...candidate, delivered: draft.revision });
        setPending(null);
      });
    }
  }, [draft, revisions, undo, pending, setUndo, setPending]);
  return {
    remember: () => {
      if (draft) setPending({ before: draft, seen: revisions.map((r) => r.requestId) });
    },
    before: undo?.before,
    delivered: undo?.delivered,
    canUndo: !!draft && undo?.delivered !== undefined && draft.revision === undo.delivered,
    clear: () => {
      setUndo(null);
      setPending(null);
    },
  };
}
