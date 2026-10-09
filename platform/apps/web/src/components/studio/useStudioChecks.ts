import { useCallback, useEffect, useRef, useState, type RefObject } from 'react';
import { request } from '../../api.ts';

/** A superseded check read must not hide a newer pending report and stop polling. */
export function useStudioChecks<T>(
  url: string,
  project: RefObject<{ revision: number } | undefined>,
) {
  const [checks, setChecks] = useState<T[]>([]);
  const generation = useRef(0);
  const invalidate = useCallback(() => {
    ++generation.current;
  }, []);
  useEffect(() => invalidate, [url, invalidate]);
  const refresh = useCallback(async () => {
    const token = ++generation.current;
    const revision = project.current?.revision;
    const value = await request<{ items: T[] }>('GET', url + '/checks');
    if (token === generation.current && revision === project.current?.revision)
      setChecks(value.items);
  }, [url, project]);
  return { checks, refresh, invalidate };
}
