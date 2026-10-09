import { MessageError } from '../../messages.ts';
import { t } from '../../messages.ts';
import { useEffect, useState } from 'react';
import { request } from '../../api.ts';
import { ROOT, type Progress } from './types.ts';

export function useStudioProgress(
  threadId: string | undefined,
  currentId: string | undefined,
  revision: number,
) {
  const [progress, setProgress] = useState<Progress>();
  const [progressError, setProgressError] = useState('');
  useEffect(() => {
    if (!threadId || !currentId) return;
    const abort = new AbortController();
    let retry: ReturnType<typeof setTimeout> | undefined;
    const load = () => {
      void request<Progress>('GET', `${ROOT}/threads/${threadId}/requests/${currentId}/progress`, {
        signal: abort.signal,
      })
        .then((value) => {
          if (abort.signal.aborted) return;
          if (
            !Array.isArray(value.stages) ||
            !Array.isArray(value.artifacts) ||
            !Array.isArray(value.checks)
          )
            throw new MessageError('Stage details are not available for this version.');
          setProgress(value);
          setProgressError('');
        })
        .catch((e) => {
          if (!abort.signal.aborted) {
            setProgressError(
              e instanceof Error ? e.message : t('Stage details could not be loaded.'),
            );
            retry = setTimeout(load, 3000);
          }
        });
    };
    load();
    return () => {
      abort.abort();
      clearTimeout(retry);
    };
  }, [threadId, currentId, revision]);
  return { progress, progressError };
}
