import { useCallback, useEffect, useState, type Dispatch, type SetStateAction } from 'react';

/** Storage is optional. A blocked browser store must never prevent editing. */
function optionalStorage(kind: 'sessionStorage' | 'localStorage') {
  return {
    getItem(key: string): string | null {
      try {
        return window[kind].getItem(key);
      } catch {
        return null;
      }
    },
    setItem(key: string, value: string) {
      try {
        window[kind].setItem(key, value);
      } catch {
        /* In-memory state remains usable. */
      }
    },
    removeItem(key: string) {
      try {
        window[kind].removeItem(key);
      } catch {
        /* Storage is optional. */
      }
    },
  };
}
export const studioSession = optionalStorage('sessionStorage');
export const studioLocal = optionalStorage('localStorage');
export function useStudioValue<T>(key: string, initial: T): [T, Dispatch<SetStateAction<T>>] {
  const [fallback] = useState(() => initial);
  const read = useCallback(
    (scope: string): T => {
      try {
        const saved = studioSession.getItem(scope);
        return saved ? (JSON.parse(saved) as T) : fallback;
      } catch {
        return fallback;
      }
    },
    [fallback],
  );
  const [stored, setStored] = useState(() => ({ key, value: read(key) }));
  if (stored.key !== key) setStored({ key, value: read(key) });
  const value = stored.key === key ? stored.value : read(key);
  const setValue = useCallback<Dispatch<SetStateAction<T>>>(
    (next) =>
      setStored((old) => ({
        key,
        value:
          typeof next === 'function'
            ? (next as (previous: T) => T)(old.key === key ? old.value : read(key))
            : next,
      })),
    [key, read],
  );
  useEffect(() => {
    if (stored.key === key) studioSession.setItem(key, JSON.stringify(stored.value));
  }, [key, stored]);
  return [value, setValue];
}
