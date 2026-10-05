import { useEffect, useRef, useState } from 'react';
import type { MusicStudioEvent } from '@glob2/protocol';
import { request } from '../../api.ts';
import { ROOT, mergeThread, type Thread } from './types.ts';

/** Replays durable events after a consistent snapshot. Network recovery never starts work. */
export function useMusicStudioStream(id: string | undefined, onChange: () => void) {
  const [thread, setThread] = useState<Thread>();
  const [connection, setConnection] = useState('');
  const [revision, setRevision] = useState(0);
  const [celebrate, setCelebrate] = useState<string>();
  const callback = useRef(onChange);
  useEffect(() => {
    callback.current = onChange;
  }, [onChange]);
  useEffect(() => {
    if (!id) return;
    const abort = new AbortController();
    let stream: EventSource | undefined;
    let retry: ReturnType<typeof setTimeout> | undefined;
    let refresh: ReturnType<typeof setTimeout> | undefined;
    let snapshotCursor = '0';
    let lastEvent = 0n;
    let recovering = false;
    let opened = false;
    let snapshotPromise: Promise<void> | undefined;
    let dirty = false;
    let baselineNeeded = false;
    const terminal = new Set<string>();
    async function snapshot(baseline = false): Promise<void> {
      baselineNeeded ||= baseline;
      if (snapshotPromise) {
        dirty = true;
        return snapshotPromise;
      }
      snapshotPromise = (async () => {
        const value = await request<Thread>('GET', `${ROOT}/threads/${id}`, {
          signal: abort.signal,
        });
        if (abort.signal.aborted) return;
        snapshotCursor = value.cursor ?? snapshotCursor;
        if (baselineNeeded)
          for (const r of value.requests)
            if (['ready', 'failed'].includes(r.status)) terminal.add(r.id);
        baselineNeeded = false;
        setThread((current) => mergeThread(current, value));
        setRevision((n) => n + 1);
      })();
      try {
        await snapshotPromise;
      } finally {
        snapshotPromise = undefined;
      }
      if (dirty && !abort.signal.aborted) {
        dirty = false;
        await snapshot(baseline);
      }
    }
    function refreshSnapshot() {
      void snapshot(recovering)
        .then(() => {
          if (!abort.signal.aborted) {
            setConnection('');
            recovering = false;
          }
        })
        .catch(() => {
          if (abort.signal.aborted) return;
          setConnection('Reconnecting to your saved project…');
          clearTimeout(retry);
          retry = setTimeout(refreshSnapshot, 3000);
        });
    }
    function connect() {
      if (abort.signal.aborted) return;
      stream?.close();
      if (typeof EventSource === 'undefined') {
        setConnection('Live updates are unavailable in this browser. Reload to refresh.');
        return;
      }
      stream = new EventSource(
        `${ROOT}/threads/${id}/events?cursor=${encodeURIComponent(snapshotCursor)}`,
      );
      stream.onopen = () => {
        setConnection('');
        if (opened || recovering) {
          recovering = true;
          void snapshot(true)
            .then(() => {
              if (abort.signal.aborted) return;
              recovering = false;
              callback.current();
            })
            .catch(() => refreshSnapshot());
        }
        opened = true;
      };
      stream.onerror = () => {
        recovering = true;
        setConnection('Connection interrupted. Reconnecting to your saved project…');
      };
      stream.onmessage = (event: MessageEvent<string>) => {
        let value: MusicStudioEvent;
        try {
          value = JSON.parse(event.data) as MusicStudioEvent;
        } catch {
          return;
        }
        const id = event.lastEventId || value.id;
        if (!/^\d+$/.test(id)) return;
        const sequence = BigInt(id);
        if (sequence <= lastEvent) return;
        lastEvent = sequence;
        if (value.type === 'complete' && value.requestId) {
          if (!recovering && !terminal.has(value.requestId) && value.payload.status === 'ready')
            setCelebrate(value.requestId);
          terminal.add(value.requestId);
        }
        clearTimeout(refresh);
        refresh = setTimeout(() => {
          refreshSnapshot();
          callback.current();
        }, 80);
      };
      stream.addEventListener('reset', () => {
        stream?.close();
        recovering = true;
        lastEvent = 0n;
        void snapshot(true)
          .then(() => {
            recovering = false;
            connect();
          })
          .catch(() => {
            if (abort.signal.aborted) return;
            retry = setTimeout(() => void start(), 3000);
          });
      });
    }
    async function start() {
      try {
        await snapshot(true);
        if (abort.signal.aborted) return;
        lastEvent = /^\d+$/.test(snapshotCursor) ? BigInt(snapshotCursor) : 0n;
        connect();
      } catch {
        if (!abort.signal.aborted) {
          setConnection('Reconnecting to your saved project…');
          retry = setTimeout(() => void start(), 3000);
        }
      }
    }
    void start();
    return () => {
      abort.abort();
      stream?.close();
      clearTimeout(retry);
      clearTimeout(refresh);
    };
  }, [id]);
  return { thread, setThread, connection, revision, celebrate };
}
