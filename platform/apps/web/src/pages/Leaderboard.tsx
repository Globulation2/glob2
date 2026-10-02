import { useState } from 'react';
import type { LeaderboardEntry } from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { Avatar, Empty, Loaded, PlayerLink } from '../components/common.tsx';
import { aiName, percent, rating, versionKey } from '../format.ts';
import { Link } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

function Rows({ entries, caption }: { entries: LeaderboardEntry[]; caption: string }) {
  return (
    <div className="table-wrap">
      <table className="data ladder">
        <caption className="sr-only">{caption}</caption>
        <thead>
          <tr>
            <th className="num" scope="col">
              Rank
            </th>
            <th scope="col">Player</th>
            <th className="num" scope="col">
              Rating
            </th>
            <th className="num" scope="col">
              Games
            </th>
            <th className="num hide-phone" scope="col">
              Win rate
            </th>
          </tr>
        </thead>
        <tbody>
          {entries.map((e) => (
            <tr
              key={`${e.rank}-${e.entity.kind}`}
              data-testid="leaderboard-row"
              className={e.rank <= 3 ? `top-${e.rank}` : undefined}
            >
              <td className="num rank">
                <span className="rank-pebble">{e.rank}</span>
              </td>
              <td className="who">
                {e.entity.kind === 'account' ? (
                  <>
                    <Avatar account={e.entity.account} size="small" />
                    <PlayerLink account={e.entity.account} />
                  </>
                ) : (
                  <>
                    <GameArt name="school" size={30} className="ai-mark" />
                    {aiName(e.entity.ai)}
                  </>
                )}
                {e.provisional && <span className="badge warn">provisional</span>}
              </td>
              <td className="num rating">{rating(e.rating)}</td>
              <td className="num">{e.games}</td>
              <td className="num hide-phone">{e.games ? percent(e.wins / e.games) : '–'}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function AiLadder({ ladder }: { ladder: string }) {
  const load = useLoad((signal) => api.aiLeaderboard(ladder, signal), [ladder]);
  return (
    <Loaded load={load}>
      {(board) =>
        board.groups.length === 0 ? (
          <p className="muted">No AI has a rating on this ladder.</p>
        ) : (
          <>
            {board.groups.map((group) => (
              <div key={versionKey(group.simVersion)} style={{ marginBottom: 'var(--sp-4)' }}>
                <div className="caption" style={{ marginBottom: 'var(--sp-2)' }}>
                  Game version {group.simVersion.versionMinor} · data{' '}
                  {group.simVersion.dataHash.slice(0, 8)}{' '}
                  {group.current ? (
                    <span className="badge ok">current</span>
                  ) : (
                    <span className="badge">older version</span>
                  )}
                </div>
                <Rows
                  entries={group.entries}
                  caption={`AI opponents, game version ${group.simVersion.versionMinor}`}
                />
              </div>
            ))}
            <p className="caption">
              Each AI revision keeps its own rating: AIs of different game versions are never
              combined.
            </p>
          </>
        )
      }
    </Loaded>
  );
}

export function Leaderboard({ queueId }: { queueId: string | undefined }) {
  const { instance } = useSession();
  const rated = instance?.queues.filter((q) => q.rated) ?? [];
  const ladder = queueId ?? rated[0]?.id;
  const [hideProvisional, setHideProvisional] = useState(false);
  const [cursor, setCursor] = useState<string[]>([]);
  const load = useLoad(
    (signal) =>
      ladder
        ? api.leaderboard(
            ladder,
            {
              provisional: hideProvisional ? 'exclude' : 'include',
              cursor: cursor.at(-1),
              limit: 50,
            },
            signal,
          )
        : Promise.resolve(undefined),
    [ladder, hideProvisional, cursor.join(',')],
  );
  const name = instance?.queues.find((q) => q.id === ladder)?.name ?? ladder;
  return (
    <>
      <div className="page-head">
        <GameArt name="warFlag" size={72} className="head-art" />
        <div className="grow">
          <h1>Leaderboard</h1>
          <p className="sub">
            Registered players, ranked by their conservative rating: it rises as the game becomes
            sure of your skill. Guests are not ranked.
          </p>
        </div>
      </div>
      {rated.length > 1 && (
        <nav className="seg" aria-label="Ladders" style={{ marginBottom: 'var(--sp-4)' }}>
          {rated.map((q) => (
            <Link
              key={q.id}
              to={`/leaderboard/${q.id}`}
              aria-current={q.id === ladder ? 'page' : undefined}
              className={q.id === ladder ? 'on' : ''}
              onClick={() => setCursor([])}
            >
              {q.name}
            </Link>
          ))}
        </nav>
      )}
      {!ladder ? (
        <p className="muted">This instance has no rated queues.</p>
      ) : (
        <>
          <div className="toolbar">
            <h2 className="grow" style={{ margin: 0 }}>
              {name}
            </h2>
            <label className="check">
              <input
                type="checkbox"
                checked={hideProvisional}
                onChange={(e) => {
                  setHideProvisional(e.target.checked);
                  setCursor([]);
                }}
              />
              Hide provisional ratings
            </label>
          </div>
          <Loaded load={load}>
            {(page) =>
              !page || page.entries.length === 0 ? (
                <Empty art="warFlag">No rated players yet.</Empty>
              ) : (
                <>
                  <Rows entries={page.entries} caption={`${name ?? 'Leaderboard'} players`} />
                  <div className="pager">
                    {cursor.length > 0 && (
                      <button className="small" onClick={() => setCursor(cursor.slice(0, -1))}>
                        Previous
                      </button>
                    )}
                    {page.nextCursor && (
                      <button
                        className="small"
                        onClick={() => setCursor([...cursor, page.nextCursor ?? ''])}
                      >
                        Next
                      </button>
                    )}
                  </div>
                </>
              )
            }
          </Loaded>
          <h2>AI opponents</h2>
          <AiLadder ladder={ladder} />
        </>
      )}
    </>
  );
}
