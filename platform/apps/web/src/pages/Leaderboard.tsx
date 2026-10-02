import { useState } from 'react';
import { simVersionKey, type LeaderboardEntry } from '@glob2/protocol';
import { api } from '../api.ts';
import { Loaded, PlayerLink } from '../components/common.tsx';
import { aiName, percent, rating } from '../format.ts';
import { Link } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

function Rows({ entries }: { entries: LeaderboardEntry[] }) {
  return (
    <div className="table-wrap">
      <table className="data">
        <thead>
          <tr>
            <th className="num" style={{ width: 50 }}>
              #
            </th>
            <th>Player</th>
            <th className="num">Rating</th>
            <th className="num">Games</th>
            <th className="num hide-phone">Win rate</th>
          </tr>
        </thead>
        <tbody>
          {entries.map((e) => (
            <tr key={`${e.rank}-${e.entity.kind}`} data-testid="leaderboard-row">
              <td className="num">{e.rank}</td>
              <td>
                {e.entity.kind === 'account' ? (
                  <PlayerLink account={e.entity.account} />
                ) : (
                  <>{aiName(e.entity.ai)}</>
                )}{' '}
                {e.provisional && <span className="badge warn">provisional</span>}
              </td>
              <td className="num">{rating(e.rating)}</td>
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
              <div key={simVersionKey(group.simVersion)} style={{ marginBottom: 12 }}>
                <div className="caption" style={{ marginBottom: 4 }}>
                  Game version {group.simVersion.versionMinor} · data{' '}
                  {group.simVersion.dataHash.slice(0, 8)}{' '}
                  {group.current ? (
                    <span className="badge ok">current</span>
                  ) : (
                    <span className="badge">older version</span>
                  )}
                </div>
                <Rows entries={group.entries} />
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
        <div className="grow">
          <h1>Leaderboard</h1>
          <div className="caption">
            Registered players, ranked by their conservative rating (it rises as the game becomes
            sure of your skill). Guests are not ranked.
          </div>
        </div>
      </div>
      {rated.length > 1 && (
        <div className="seg" role="tablist" style={{ marginBottom: 10 }}>
          {rated.map((q) => (
            <Link
              key={q.id}
              to={`/leaderboard/${q.id}`}
              role="tab"
              aria-selected={q.id === ladder}
              className={q.id === ladder ? 'on' : ''}
              onClick={() => setCursor([])}
            >
              {q.name}
            </Link>
          ))}
        </div>
      )}
      {!ladder ? (
        <p className="muted">This instance has no rated queues.</p>
      ) : (
        <>
          <div className="toolbar">
            <h2 className="grow" style={{ margin: 0 }}>
              {name}
            </h2>
            <label className="caption" style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <input
                type="checkbox"
                checked={hideProvisional}
                onChange={(e) => {
                  setHideProvisional(e.target.checked);
                  setCursor([]);
                }}
                style={{ minHeight: 0 }}
              />
              Hide provisional ratings
            </label>
          </div>
          <Loaded load={load}>
            {(page) =>
              !page || page.entries.length === 0 ? (
                <div className="list empty">No rated players yet.</div>
              ) : (
                <>
                  <Rows entries={page.entries} />
                  <div className="toolbar" style={{ marginTop: 8 }}>
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
