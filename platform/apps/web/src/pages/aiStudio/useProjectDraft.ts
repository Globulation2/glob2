import { useCallback, useEffect, useRef, useState } from 'react';
import type { AiStudioDetail } from '@glob2/protocol';
import { ApiError, request } from '../../api.ts';

/** Coordinates editable local text with immutable server revisions. A refresh started
 * before a write may finish afterwards, so both request order and revision order
 * fence its response before it can touch the draft or resumable event cursor. */
export function useProjectDraft(url: string, recoveryKey: string, onError: (text: string) => void) {
  const [project, setProject] = useState<AiStudioDetail>();
  const [source, setSource] = useState('');
  const [saved, setSaved] = useState('Loading…');
  const [conflict, setConflict] = useState(false);
  const known = useRef<AiStudioDetail | undefined>(undefined);
  const draft = useRef('');
  const saving = useRef<Promise<void> | undefined>(undefined);
  const cursor = useRef('0');
  const generation = useRef(0);
  const recovered = useRef(false);
  const reportError = useRef(onError);
  useEffect(() => {
    reportError.current = onError;
  }, [onError]);
  const invalidate = useCallback(() => {
    ++generation.current;
  }, []);

  const remember = useCallback(() => {
    if (!recoveryKey || !known.current) return;
    try {
      if (draft.current === known.current.current.source) localStorage.removeItem(recoveryKey);
      else
        localStorage.setItem(
          recoveryKey,
          JSON.stringify({
            revision: known.current.revision,
            source: draft.current,
          }),
        );
    } catch {
      // Storage is optional; server autosave still works in private browsing.
    }
  }, [recoveryKey]);

  const change = useCallback(
    (text: string) => {
      draft.current = text;
      setSource(text);
      setSaved(text === known.current?.current.source ? 'Saved' : 'Unsaved changes');
      remember();
    },
    [remember],
  );

  const refresh = useCallback(async () => {
    const requestGeneration = ++generation.current;
    const value = await request<AiStudioDetail>('GET', url);
    const old = known.current;
    if (
      requestGeneration !== generation.current ||
      (old && (value.revision < old.revision || BigInt(value.cursor) < BigInt(cursor.current)))
    )
      return;
    if (!old || draft.current === old.current.source) {
      draft.current = value.current.source;
      setSource(value.current.source);
      setSaved('Saved');
    } else if (old.revision !== value.revision) {
      setConflict(true);
      reportError.current(
        'The draft changed elsewhere. Download your local edits before reloading.',
      );
    }
    known.current = value;
    cursor.current = value.cursor;
    if (!recovered.current && recoveryKey) {
      recovered.current = true;
      try {
        const local = JSON.parse(localStorage.getItem(recoveryKey) ?? 'null') as {
          revision: number;
          source: string;
        } | null;
        if (local && typeof local.source === 'string' && local.source !== value.current.source) {
          draft.current = local.source;
          setSource(local.source);
          setSaved('Recovered local edits');
          if (local.revision !== value.revision) {
            setConflict(true);
            reportError.current(
              'Recovered edits belong to an older revision. Download them before reloading.',
            );
          }
        }
      } catch {
        // Recovery is best effort; malformed browser storage cannot replace server data.
      }
    }
    // Preserve the original base of a conflicting draft until the user resolves it.
    if (draft.current === value.current.source) remember();
    setProject(value);
    return value;
  }, [url, recoveryKey, remember]);

  const save = useCallback(async () => {
    // Recheck after each waiter: several callers may all wait for the same save.
    while (saving.current) await saving.current;
    const p = known.current;
    if (!p || draft.current === p.current.source) return;
    const text = draft.current;
    ++generation.current;
    setSaved('Saving…');
    const task = (async () => {
      const result = await request<{ revision: number }>('PATCH', url, {
        body: { expectedRevision: p.revision, source: text },
      });
      known.current = {
        ...p,
        revision: result.revision,
        current: { ...p.current, source: text, revision: result.revision },
      };
      remember();
      await refresh();
    })();
    saving.current = task;
    try {
      await task;
    } catch (e) {
      setSaved('Not saved');
      // Invalid intermediate text and network failures must leave the editor usable.
      if (e instanceof ApiError && e.status === 409) setConflict(true);
      reportError.current(e instanceof Error ? e.message : String(e));
      throw e;
    } finally {
      saving.current = undefined;
    }
  }, [url, refresh, remember]);

  const reload = useCallback(async () => {
    ++generation.current;
    known.current = undefined;
    try {
      localStorage.removeItem(recoveryKey);
    } catch {
      /* Optional recovery. */
    }
    setConflict(false);
    return refresh();
  }, [refresh, recoveryKey]);

  useEffect(() => {
    const before = (event: BeforeUnloadEvent) => {
      if (known.current && draft.current !== known.current.current.source) {
        event.preventDefault();
        event.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', before);
    return () => {
      invalidate();
      window.removeEventListener('beforeunload', before);
    };
  }, [invalidate]);

  return {
    project,
    source,
    saved,
    conflict,
    known,
    draft,
    saving,
    cursor,
    change,
    refresh,
    save,
    reload,
  };
}
