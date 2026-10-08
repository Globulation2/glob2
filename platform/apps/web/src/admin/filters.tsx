import { useState, type Dispatch, type SetStateAction } from 'react';
import { useRouter } from '../router.tsx';

export function useAdminFilters(defaults: Record<string, string> = {}) {
  const { location, navigate } = useRouter();
  const values = { ...defaults, ...Object.fromEntries(location.search) };
  const set = (patch: Record<string, string | undefined>) => {
    const params = new URLSearchParams(location.search);
    if (!Object.hasOwn(patch, 'cursor')) params.delete('cursor');
    for (const [key, value] of Object.entries(patch)) {
      if (value) params.set(key, value);
      else params.delete(key);
    }
    navigate(location.path + (params.size ? '?' + params.toString() : ''));
  };
  return { values, set };
}
export function PageControls({ nextCursor }: { nextCursor?: string }) {
  const { values, set } = useAdminFilters();
  return (
    <div className="toolbar">
      {values['cursor'] && <button onClick={() => set({ cursor: undefined })}>First page</button>}
      {nextCursor && <button onClick={() => set({ cursor: nextCursor })}>Next page</button>}
    </div>
  );
}

/** Keep editable filter fields aligned with Back/Forward and deep-link changes. */
export function useFilterDraft<T>(source: T): [T, Dispatch<SetStateAction<T>>] {
  const signature = JSON.stringify(source);
  const [draft, setDraft] = useState({ signature, value: source });
  if (draft.signature !== signature) setDraft({ signature, value: source });
  return [
    draft.signature === signature ? draft.value : source,
    (value) =>
      setDraft((current) => ({
        signature,
        value: typeof value === 'function' ? (value as (previous: T) => T)(current.value) : value,
      })),
  ];
}
