import { useState } from 'react';
import type { MatchSummary } from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { Loaded, MatchListView } from '../components/common.tsx';
import { useLoad, useSession } from '../state.tsx';

/** Recent public matches: quick-match games and public rooms. */
export function Matches() {
  const { instance } = useSession();
  const [queue, setQueue] = useState('');
  const [more, setMore] = useState(0);
  const load = useLoad(
    async (signal) => {
      const items: MatchSummary[] = [];
      let cursor: string | undefined;
      for (let i = 0; i <= more; i++) {
        const page = await api.matches({ queue, cursor, limit: 25 }, signal);
        items.push(...page.items);
        cursor = page.nextCursor;
        if (!cursor) break;
      }
      return { items, cursor };
    },
    [queue, more],
  );
  const filters = [
    { id: '', name: 'All' },
    ...(instance?.queues.map((q) => ({ id: q.id, name: q.name })) ?? []),
    { id: 'room', name: 'Rooms' },
  ];
  return (
    <>
      <div className="page-head">
        <GameArt name="swarm" size={72} className="head-art" />
        <div className="grow">
          <h1>Recent matches</h1>
          <p className="sub">Quick-match games and public rooms, newest first.</p>
        </div>
      </div>
      <div
        className="seg"
        role="group"
        aria-label="Show matches from"
        style={{ marginBottom: 'var(--sp-4)' }}
      >
        {filters.map((f) => (
          <button
            key={f.id}
            className={queue === f.id ? 'on' : ''}
            aria-pressed={queue === f.id}
            onClick={() => {
              setQueue(f.id);
              setMore(0);
            }}
          >
            {f.name}
          </button>
        ))}
      </div>
      <Loaded load={load}>
        {(data) => (
          <>
            <MatchListView matches={data.items} />
            {data.cursor && (
              <button
                className="small"
                style={{ marginTop: 'var(--sp-3)' }}
                onClick={() => setMore(more + 1)}
              >
                Show more
              </button>
            )}
          </>
        )}
      </Loaded>
    </>
  );
}
