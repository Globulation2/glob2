import { t, message as sourceMessage, translateError } from '../../messages.ts';
import { useCallback, useEffect, useRef, useState } from 'react';
import type {
  BuildingAiStudioProgress,
  BuildingAiStudioThread,
  BuildingDraft,
} from '@glob2/protocol';
import { request } from '../../api.ts';

export const BUILDING_STUDIO_ROOT = '/api/v1/ai-building-studio';
export type BuildingWallet = {
  enabled: boolean;
  available: number;
  reserved: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
};

/** SSE wakes refreshes; polling recovers missed events and browsers without SSE. */
export function useStudioSnapshot(
  accountId: string | undefined,
  projectId: string | undefined,
  onThread: (thread: BuildingAiStudioThread) => void,
) {
  const [wallet, setWallet] = useState<BuildingWallet>(),
    [projects, setProjects] = useState<{ id: string; title: string }[]>([]),
    [thread, setThread] = useState<BuildingAiStudioThread>(),
    [draft, setDraft] = useState<BuildingDraft>(),
    [progress, setProgress] = useState<BuildingAiStudioProgress>(),
    [loading, setLoading] = useState(true),
    [loadError, setLoadError] = useState<string | Error>(''),
    [connection, setConnection] = useState('');
  const serial = useRef(0);
  const refresh = useCallback(
    async (signal?: AbortSignal) => {
      const ticket = ++serial.current;
      try {
        const wallet = await request<BuildingWallet>('GET', BUILDING_STUDIO_ROOT + '/account', {
          signal,
        });
        if (signal?.aborted || ticket !== serial.current) return;
        if (!projectId) {
          const list = await request<{ items: { id: string; title: string }[] }>(
            'GET',
            BUILDING_STUDIO_ROOT + '/threads',
            { signal },
          );
          if (signal?.aborted || ticket !== serial.current) return;
          setProjects(list.items);
        } else {
          const thread = await request<BuildingAiStudioThread>(
            'GET',
            `${BUILDING_STUDIO_ROOT}/threads/${projectId}`,
            { signal },
          );
          const last = thread.requests.at(-1);
          const [draft, progress] = await Promise.all([
            request<BuildingDraft>('GET', '/api/v1/building-drafts/' + thread.draftId, { signal }),
            last
              ? request<BuildingAiStudioProgress>(
                  'GET',
                  `${BUILDING_STUDIO_ROOT}/threads/${projectId}/requests/${last.id}/progress`,
                  { signal },
                )
              : undefined,
          ]);
          // Events and polling may overlap. Never replace a newer snapshot with
          // an older response, including its draft revision used for writes.
          if (signal?.aborted || ticket !== serial.current) return;
          setThread(thread);
          setDraft(draft);
          setProgress(progress);
          onThread(thread);
        }
        setWallet(wallet);
        setLoadError('');
        setLoading(false);
      } catch (error) {
        if (!signal?.aborted && ticket === serial.current) {
          setLoadError(
            error instanceof Error ? error : sourceMessage('Could not load your project.'),
          );
          setLoading(false);
        }
        throw error;
      }
    },
    [projectId, onThread],
  );
  useEffect(() => {
    if (!accountId) return;
    const abort = new AbortController();
    let stream: EventSource | undefined;
    const update = () => {
      void refresh(abort.signal).catch(() => undefined);
    };
    update();
    if (projectId && typeof EventSource !== 'undefined') {
      stream = new EventSource(`${BUILDING_STUDIO_ROOT}/threads/${projectId}/events`);
      stream.onmessage = update;
      stream.onopen = () => {
        setConnection('');
        update();
      };
      stream.onerror = () => setConnection(sourceMessage('Reconnecting to your saved project…'));
      stream.addEventListener('reset', update);
    }
    const poll = setInterval(update, 10000);
    return () => {
      abort.abort();
      stream?.close();
      clearInterval(poll);
    };
  }, [accountId, projectId, refresh]);
  return {
    wallet,
    projects,
    thread,
    draft,
    progress,
    loading,
    loadError: typeof loadError === 'string' ? t(loadError) : translateError(loadError),
    connection: t(connection),
    refresh,
  };
}
