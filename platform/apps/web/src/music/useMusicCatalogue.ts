import { useCallback, useEffect, useRef, useState } from 'react';
import type { MusicList, MusicRelease } from '@glob2/protocol';
import { request } from '../api.ts';

/** A filter change cancels both the current search and any pending continuation. */
export function useMusicCatalogue(filters: string) {
  const [revision, setRevision] = useState(0);
  const [items, setItems] = useState<MusicRelease[]>([]);
  const [next, setNext] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [loadedFilters, setLoadedFilters] = useState<string | null>(null);
  const reload = useCallback(() => {
    setLoadedFilters(null);
    setRevision((value) => value + 1);
  }, []);
  const active = useRef<AbortController | null>(null);
  const currentFilters = useRef(filters);
  useEffect(() => {
    currentFilters.current = filters;
    active.current?.abort();
    const controller = new AbortController();
    active.current = controller;
    const timer = setTimeout(() => {
      setError('');
      setLoading(true);
      setNext(null);
      void request<MusicList>('GET', `/api/v1/music?${filters}`, { signal: controller.signal })
        .then((data) => {
          if (controller.signal.aborted) return;
          setLoadedFilters(filters);
          setItems(data.items);
          setNext(data.next);
          setError('');
        })
        .catch((error: unknown) => {
          if (!controller.signal.aborted) {
            setLoadedFilters(filters);
            setError(error instanceof Error ? error.message : String(error));
          }
        })
        .finally(() => {
          if (!controller.signal.aborted) setLoading(false);
        });
    }, 0);
    return () => {
      clearTimeout(timer);
      controller.abort();
      active.current?.abort();
    };
  }, [filters, revision]);

  const more = useCallback(async () => {
    if (!next || loading || loadedFilters !== filters || currentFilters.current !== filters) return;
    active.current?.abort();
    const controller = new AbortController();
    active.current = controller;
    setLoading(true);
    try {
      const data = await request<MusicList>(
        'GET',
        `/api/v1/music?${filters}&cursor=${encodeURIComponent(next)}`,
        { signal: controller.signal },
      );
      if (controller.signal.aborted || currentFilters.current !== filters) return;
      setItems((old) => [...old, ...data.items]);
      setNext(data.next);
      setError('');
    } catch (error) {
      if (!controller.signal.aborted)
        setError(error instanceof Error ? error.message : String(error));
    } finally {
      if (!controller.signal.aborted) setLoading(false);
    }
  }, [filters, loadedFilters, next, loading]);
  return {
    items: loadedFilters === filters ? items : [],
    setItems,
    next: loadedFilters === filters ? next : null,
    loading: loading || loadedFilters !== filters,
    error: loadedFilters === filters ? error : '',
    more,
    reload,
  };
}
