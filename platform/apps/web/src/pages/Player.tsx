import { useState } from 'react';
import type {
  EconomyCurve,
  MatchSummary,
  PlayerAggregates,
  PlayerProfile,
  RatingHistoryPoint,
  WinRate,
} from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { seriesInk } from '../colors.ts';
import { LineChart } from '../components/LineChart.tsx';
import { Avatar, Loaded, MatchListView } from '../components/common.tsx';
import { date, duration, percent, queueName, rating, tickTime } from '../format.ts';
import { Link } from '../router.tsx';
import { isModerator, useLoad, useSession } from '../state.tsx';
import { useTheme } from '../theme.tsx';

function RatingGraph({ history, queues }: { history: RatingHistoryPoint[]; queues: string[] }) {
  const { theme } = useTheme();
  if (history.length === 0) return null;
  const ladders = [...new Set(history.map((p) => p.ladder))];
  const series = ladders.map((ladder, i) => {
    const points = history.filter((p) => p.ladder === ladder);
    return {
      name: queues[i] ?? ladder,
      color: seriesInk(i, theme),
      points: points.map((p) => ({ x: Date.parse(p.at), y: p.after })),
    };
  });
  return (
    <LineChart
      title="Rating after each rated match"
      series={series}
      zeroBased={false}
      dots
      xFormat={(x) => date(new Date(x).toISOString())}
      yFormat={(y) => rating(y)}
    />
  );
}

function WinRates({ title, rows }: { title: string; rows: WinRate[] }) {
  if (rows.length === 0) return null;
  return (
    <div className="card">
      <h3>{title}</h3>
      <table className="data">
        <caption className="sr-only">Win rate {title.toLowerCase()}</caption>
        <tbody>
          {rows.slice(0, 6).map((row) => (
            <tr key={row.key}>
              <td className="ell" style={{ maxWidth: 180 }}>
                {row.mapId ? (
                  <Link to={`/maps/${row.mapId}`}>{row.label ?? row.key}</Link>
                ) : (
                  (row.label ?? (row.dimension === 'map' ? 'Private or uploaded map' : row.key))
                )}
              </td>
              <td className="num">
                {row.wins}–{row.games - row.wins}
              </td>
              <td className="num" style={{ width: 70 }}>
                {percent(row.winRate)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function Economy({ curve }: { curve: EconomyCurve }) {
  const { theme } = useTheme();
  const at = (pick: (p: EconomyCurve['points'][number]) => number) =>
    curve.points.map((p) => ({ x: p.tick, y: pick(p) }));
  return (
    <div className="grid2">
      <LineChart
        title="Units: latest verified match against your average"
        series={[
          { name: 'This match', color: seriesInk(0, theme), points: at((p) => p.units) },
          {
            name: 'Your average',
            color: seriesInk(1, theme),
            points: at((p) => p.averageUnits),
            dashed: true,
          },
        ]}
        xFormat={tickTime}
        height={200}
      />
      <LineChart
        title="Buildings: latest verified match against your average"
        series={[
          { name: 'This match', color: seriesInk(0, theme), points: at((p) => p.buildings) },
          {
            name: 'Your average',
            color: seriesInk(1, theme),
            points: at((p) => p.averageBuildings),
            dashed: true,
          },
        ]}
        xFormat={tickTime}
        height={200}
      />
    </div>
  );
}

function Aggregates({ aggregates }: { aggregates: PlayerAggregates }) {
  const by = (dimension: WinRate['dimension']) =>
    aggregates.winRates.filter((r) => r.dimension === dimension);
  return (
    <>
      <h2>Last {aggregates.windowDays} days</h2>
      <div className="grid3">
        <WinRates title="By queue" rows={by('queue')} />
        <WinRates title="By map" rows={by('map')} />
        <WinRates title="By generator" rows={by('generator')} />
      </div>
      {aggregates.economy && (
        <>
          <h2>
            Economy{' '}
            <Link className="caption" to={`/matches/${aggregates.economy.matchId}`}>
              (match)
            </Link>
          </h2>
          <Economy curve={aggregates.economy} />
        </>
      )}
    </>
  );
}

function Tiles({ profile }: { profile: PlayerProfile }) {
  const { instance } = useSession();
  const a = profile.aggregates;
  const best = a?.winRates
    .filter((r) => r.dimension !== 'queue' && r.games >= 2)
    .sort((x, y) => y.winRate - x.winRate || y.games - x.games)[0];
  return (
    <div className="tiles">
      {profile.ratings.map((r) => (
        <div className="tile" key={r.ladder} data-testid="rating-tile">
          <GameArt name="warFlag" className="art" size={64} />
          <div className="caption">{queueName(instance?.queues, r.ladder)}</div>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, flexWrap: 'wrap' }}>
            <span className="v">{rating(r.rating)}</span>
            {r.rank !== undefined && <span className="caption">#{r.rank}</span>}
            {r.provisional && <span className="badge warn">provisional</span>}
          </div>
          <div className="caption">
            {r.wins} W · {r.games - r.wins} L
          </div>
        </div>
      ))}
      {a && a.games > 0 && (
        <div className="tile">
          <GameArt name="fruit" className="art" size={64} />
          <div className="caption">Win rate, {a.windowDays} days</div>
          <div className="v">{percent(a.wins / a.games)}</div>
          <div className="caption">
            {a.wins} W · {a.losses} L
            {a.games - a.wins - a.losses > 0 ? ` · ${a.games - a.wins - a.losses} unresolved` : ''}
          </div>
        </div>
      )}
      {a?.medianTicks !== undefined && (
        <div className="tile">
          <GameArt name="racetrack" className="art" size={64} />
          <div className="caption">Typical game</div>
          <div className="v">{duration(a.medianTicks)}</div>
          <div className="caption">median length</div>
        </div>
      )}
      {best && (
        <div className="tile">
          <GameArt name="clearingFlag" className="art" size={64} />
          <div className="caption">Best {best.dimension}</div>
          <div
            style={{ fontSize: 'var(--text-lg)', fontWeight: 750, margin: '4px 0' }}
            className="ell"
          >
            {best.label ?? best.key}
          </div>
          <div className="caption">
            {best.wins} W · {best.games - best.wins} L
          </div>
        </div>
      )}
    </div>
  );
}

function Matches({ id, initial: first }: { id: string; initial: MatchSummary[] }) {
  const { instance } = useSession();
  const [queue, setQueue] = useState('');
  const [pages, setPages] = useState<string[]>([]);
  const load = useLoad(
    async (signal) => {
      const out: MatchSummary[] = [];
      let cursor: string | undefined;
      for (let i = 0; i <= pages.length; i++) {
        const page = await api.playerMatches(id, { queue, cursor, limit: 20 }, signal);
        out.push(...page.items);
        cursor = page.nextCursor;
        if (!cursor) break;
      }
      return { items: out, nextCursor: cursor };
    },
    [id, queue, pages.length],
  );
  const filters = [
    { id: '', name: 'All' },
    ...(instance?.queues.map((q) => ({ id: q.id, name: q.name })) ?? []),
    { id: 'room', name: 'Rooms' },
  ];
  return (
    <>
      <div className="toolbar">
        <h2 className="grow" style={{ margin: 0 }}>
          Matches
        </h2>
        <span className="seg" role="group" aria-label="Show matches from">
          {filters.map((f) => (
            <button
              key={f.id}
              className={queue === f.id ? 'on' : ''}
              aria-pressed={queue === f.id}
              onClick={() => {
                setQueue(f.id);
                setPages([]);
              }}
            >
              {f.name}
            </button>
          ))}
        </span>
      </div>
      {load.status === 'loading' && queue === '' && pages.length === 0 ? (
        <MatchListView matches={first} accountId={id} />
      ) : (
        <Loaded load={load}>
          {(data) => (
            <>
              <MatchListView matches={data.items} accountId={id} />
              {data.nextCursor && (
                <button
                  className="small"
                  style={{ marginTop: 'var(--sp-3)' }}
                  onClick={() => setPages([...pages, data.nextCursor ?? ''])}
                >
                  Show more
                </button>
              )}
            </>
          )}
        </Loaded>
      )}
    </>
  );
}

export function Player({ id }: { id: string }) {
  const { instance, account } = useSession();
  const load = useLoad((signal) => api.player(id, signal), [id]);
  return (
    <Loaded load={load}>
      {(profile) => {
        const ladderNames = [...new Set(profile.ratingHistory.map((p) => p.ladder))].map((l) =>
          queueName(instance?.queues, l),
        );
        return (
          <>
            <div className="profile-head">
              <Avatar account={profile.account} size="large" />
              <div className="grow">
                <h1 data-testid="player-name">{profile.account.displayName}</h1>
                <div className="caption" style={{ fontSize: 'var(--text-md)' }}>
                  {profile.account.kind === 'guest' ? 'Guest player' : 'Registered player'} ·
                  playing since {date(profile.account.createdAt)}
                  {profile.status && profile.status !== 'active' && (
                    <>
                      {' '}
                      <span className="badge bad">{profile.status}</span>
                    </>
                  )}
                </div>
              </div>
              {isModerator(account) && (
                <Link
                  className="btn small"
                  to={`/admin/accounts?q=${encodeURIComponent(profile.account.displayName)}`}
                >
                  Moderate
                </Link>
              )}
            </div>
            {profile.detail === 'minimal' ? (
              <p className="notice">
                Guests are not ranked and keep no statistics. Their matches still appear below.
              </p>
            ) : (
              <>
                <Tiles profile={profile} />
                {profile.ratingHistory.length > 0 && (
                  <>
                    <h2>Rating</h2>
                    <RatingGraph history={profile.ratingHistory} queues={ladderNames} />
                  </>
                )}
                {profile.aggregates && profile.aggregates.games > 0 && (
                  <Aggregates aggregates={profile.aggregates} />
                )}
              </>
            )}
            <Matches id={profile.account.id} initial={profile.recentMatches} />
          </>
        );
      }}
    </Loaded>
  );
}
