import { useState, type CSSProperties } from 'react';
import {
  CONNECTION_METRICS,
  CONNECTION_RATING_LABELS,
  formatConnectionValue,
  rateConnection,
  type ConnectionMetric,
  type MatchDetail,
  type MatchParticipant,
  type ParticipantNetwork,
  type TeamTimelinePoint,
} from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { seriesInk, teamInk } from '../colors.ts';
import { LineChart } from '../components/LineChart.tsx';
import { Loaded, StatusBadge, TableWrap, VerificationBadge } from '../components/common.tsx';
import {
  aiName,
  dateTime,
  duration,
  participantName,
  queueName,
  rating,
  signed,
  teamColor,
  tickTime,
} from '../format.ts';
import { Link } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import { useTheme } from '../theme.tsx';

/** The browser build of the game; `?replay=<url>` opens a replay (browser/shell.html). */
export function watchUrl(replayUrl: string): string {
  return `/play/?replay=${encodeURIComponent(replayUrl)}`;
}

const VERDICT_TEXT: Record<string, string> = {
  verified: 'The server replayed this match and every player’s game agreed with it.',
  pending: 'The server is checking this match. Ratings change once it is verified.',
  diverged:
    'A player’s game disagreed with the server’s replay of the match; the server’s result counts.',
  unverifiable: 'The server could not reproduce this match, so it changes no rating.',
  not_applicable: 'This match is not checked.',
  failed:
    'The server could not finish checking this match, so it changes no rating for now. An administrator can run the check again.',
};

/** Teams on the match's map: the engine colours teams by this count. */
function teamCount(detail: MatchDetail): number {
  return Math.max(
    detail.setup?.teams?.length ?? 0,
    ...detail.match.participants.map((p) => p.team + 1),
    ...detail.teams.map((t) => t.team + 1),
  );
}

function teamName(detail: MatchDetail, team: number): string {
  const members = detail.match.participants.filter((p) => p.team === team);
  return members.length ? members.map(participantName).join(' + ') : `Team ${team + 1}`;
}

function TeamCards({ detail }: { detail: MatchDetail }) {
  const count = teamCount(detail);
  const teams = [...new Set(detail.match.participants.map((p) => p.team))].sort((a, b) => a - b);
  if (teams.length < 2) return null;
  return (
    <div className="teams">
      {teams.map((team) => {
        const stats = detail.teams.find((t) => t.team === team);
        const members = detail.match.participants.filter((p) => p.team === team);
        const outcome = members[0]?.outcome ?? stats?.outcome;
        const won = outcome === 'won';
        return (
          <div
            key={team}
            className={`team-card${won ? ' won' : ''}`}
            style={{ '--team': teamColor(team, count) } as CSSProperties}
          >
            {won && <GameArt name="clearingFlag" size={44} className="crown" />}
            <div className="outcome">
              {outcome ? (won ? 'Winner' : outcome === 'draw' ? 'Draw' : outcome) : 'Team'}
            </div>
            <div style={{ fontWeight: 750, fontSize: 'var(--text-lg)' }}>
              {teamName(detail, team)}
            </div>
            {stats && <div className="caption">Prestige {stats.prestige}</div>}
          </div>
        );
      })}
    </div>
  );
}

function Participants({ detail }: { detail: MatchDetail }) {
  const count = teamCount(detail);
  const rows = [...detail.match.participants].sort((a, b) => a.team - b.team || a.seat - b.seat);
  const rejections = new Map(
    (detail.verificationDetail?.orderRejections ?? []).map((r) => [r.seat, r]),
  );
  const diverged = new Set(detail.verificationDetail?.divergedSeats ?? []);
  return (
    <TableWrap label="Players and results">
      <table className="data">
        <thead>
          <tr>
            <th>Player</th>
            <th>Result</th>
            <th className="num">Rating</th>
            <th className="num hide-phone">Change</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((p: MatchParticipant) => {
            const r = p.rating;
            return (
              <tr key={p.seat} data-testid="participant">
                <td>
                  <span className="sw" style={{ background: teamColor(p.team, count) }} />{' '}
                  {p.kind === 'human' && p.accountId ? (
                    <Link to={`/players/${p.accountId}`}>{p.displayName}</Link>
                  ) : p.kind === 'ai' ? (
                    <>
                      {aiName(p.ai)} <span className="badge">AI</span>
                    </>
                  ) : (
                    p.displayName
                  )}
                  {p.disconnects > 0 && (
                    <span className="caption">
                      {' '}
                      · {p.disconnects} disconnect{p.disconnects > 1 ? 's' : ''}
                    </span>
                  )}
                  {diverged.has(p.seat) && <span className="badge bad"> diverged</span>}
                  {rejections.has(p.seat) && <span className="badge warn"> refused orders</span>}
                </td>
                <td>{p.outcome ?? '–'}</td>
                <td className="num">
                  {r ? (
                    <>
                      {rating(r.before)} → {rating(r.after)}
                      {r.provisional && <span className="caption"> (provisional)</span>}
                    </>
                  ) : (
                    '–'
                  )}
                </td>
                <td className="num hide-phone">
                  {r ? (
                    <span className={r.after >= r.before ? 'up' : 'dn'}>
                      {signed(r.after - r.before)}
                    </span>
                  ) : (
                    ''
                  )}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </TableWrap>
  );
}

const QUALITY_BADGE: Record<ParticipantNetwork['quality'], string> = {
  good: 'badge ok',
  fair: 'badge warn',
  poor: 'badge bad',
};

const QUALITY_LABEL = CONNECTION_RATING_LABELS;

/**
 * One measured quantity with its unit and its word from the shared table
 * ("84 ms · Good"), the 95th percentile underneath. The same words and limits
 * as the in-game connection panel (docs/multiplayer/connection-quality.md).
 */
function Measured({
  metric,
  spread,
}: {
  metric: ConnectionMetric;
  spread?: { p50: number; p95: number };
}) {
  if (!spread) return <>–</>;
  return (
    <>
      {formatConnectionValue(metric, spread.p50)}
      <span className="caption"> · {QUALITY_LABEL[rateConnection(metric, spread.p50)]}</span>
      <br />
      <span className="caption">95%: {formatConnectionValue(metric, spread.p95)}</span>
    </>
  );
}

/** "Good under 150 ms, Fair under 300 ms, Poor from 300 ms." */
function limits(metric: ConnectionMetric): string {
  const t = CONNECTION_METRICS[metric];
  const v = (ms: number) => formatConnectionValue(metric, ms).replace(/\.0 s$/, ' s');
  return `${QUALITY_LABEL.good} under ${v(t.fairMs)}, ${QUALITY_LABEL.fair.toLowerCase()} under ${v(t.poorMs)}, ${QUALITY_LABEL.poor.toLowerCase()} from ${v(t.poorMs)}.`;
}

function seconds(ms: number): string {
  if (ms === 0) return '–';
  return ms < 10_000 ? `${(ms / 1000).toFixed(1)} s` : `${Math.round(ms / 1000)} s`;
}

/** Each human player's connection as the relay measured it (MatchDetail.network). */
function Connection({ detail }: { detail: MatchDetail }) {
  const rows = detail.network ?? [];
  if (rows.length === 0) return null;
  const player = (seat: number) => {
    const p = detail.match.participants.find((x) => x.seat === seat);
    return p ? participantName(p) : `Seat ${seat + 1}`;
  };
  const count = teamCount(detail);
  const team = (seat: number) => detail.match.participants.find((x) => x.seat === seat)?.team;
  return (
    <section className="net-quality" data-testid="network">
      <h2>Connection</h2>
      <TableWrap label="Connection quality per player" stack>
        <table className="data">
          <thead>
            <tr>
              <th>Player</th>
              <th>Quality</th>
              <th className="num">Ping</th>
              <th className="num">Behind</th>
              <th className="num">Disconnects</th>
              <th className="num">Offline</th>
              <th className="num">Delayed orders</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((n) => {
              const t = team(n.seat);
              return (
                <tr key={n.seat} data-testid="network-row">
                  <td data-label="Player" className="stack-head">
                    {t !== undefined && (
                      <>
                        <span className="sw" style={{ background: teamColor(t, count) }} />{' '}
                      </>
                    )}
                    {player(n.seat)}
                    {n.rejoins > 0 && (
                      <>
                        {' '}
                        <span className="badge warn">resynced</span>
                      </>
                    )}
                    {n.leftBy === 'grace' && (
                      <>
                        {' '}
                        <span className="badge bad">dropped</span>
                      </>
                    )}
                  </td>
                  <td data-label="Quality">
                    <span className={QUALITY_BADGE[n.quality]}>{QUALITY_LABEL[n.quality]}</span>
                  </td>
                  <td className="num" data-label="Ping">
                    <Measured metric="ping" spread={n.rttMs} />
                  </td>
                  <td className="num" data-label="Behind">
                    <Measured metric="behind" spread={n.lagMs} />
                  </td>
                  <td className="num" data-label="Disconnects">
                    {n.disconnects}
                  </td>
                  <td className="num" data-label="Offline">
                    {seconds(n.offlineMs)}
                  </td>
                  <td className="num" data-label="Delayed orders">
                    {n.ordersDeferred}
                    <span className="caption"> / {n.ordersSequenced}</span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </TableWrap>
      <p className="caption" data-testid="network-legend">
        Measured by the relay; typical value first, then the 95th percentile. Ping: round trip
        between the player and the relay. {limits('ping')} Behind: how far the player’s game ran
        behind the match clock, their input delay included. {limits('behind')} Quality is the worst
        of these and of reconnects, time offline and delayed orders. Offline: time disconnected
        before reconnecting. Delayed orders: orders that ran a tick later than asked.
      </p>
    </section>
  );
}

function Timelines({ detail }: { detail: MatchDetail }) {
  const { theme } = useTheme();
  const count = teamCount(detail);
  const teams = detail.teams.filter((t) => t.timeline.length > 0);
  if (teams.length === 0) {
    return (
      <p className="muted">
        {detail.match.verification === 'pending'
          ? 'Charts appear once the server has replayed the match.'
          : 'No timeline was recorded for this match.'}
      </p>
    );
  }
  const chart = (title: string, pick: (p: TeamTimelinePoint) => number) => (
    <LineChart
      title={title}
      height={190}
      xFormat={tickTime}
      series={teams.map((t) => ({
        name: teamName(detail, t.team),
        color: teamInk(t.team, count, theme),
        points: t.timeline.map((p) => ({ x: p.tick, y: pick(p) })),
      }))}
    />
  );
  return (
    <div className="charts" data-testid="timelines">
      {chart('Units', (p) => p.units)}
      {chart('Buildings', (p) => p.buildings)}
      {chart('Prestige', (p) => p.prestige)}
    </div>
  );
}

function Economy({ detail }: { detail: MatchDetail }) {
  const { theme } = useTheme();
  const curves = detail.economy ?? [];
  const [pick, setPick] = useState(0);
  const curve = curves[pick];
  if (!curve) return null;
  const name =
    detail.match.participants.find((p) => p.accountId === curve.accountId)?.displayName ?? 'Player';
  return (
    <>
      <div className="toolbar" style={{ marginTop: 'var(--sp-6)' }}>
        <h2 className="grow" style={{ margin: 0 }}>
          Economy against each player’s average
        </h2>
        {curves.length > 1 && (
          <select
            aria-label="Player"
            value={pick}
            onChange={(e) => setPick(Number(e.target.value))}
          >
            {curves.map((c, i) => (
              <option key={c.accountId} value={i}>
                {detail.match.participants.find((p) => p.accountId === c.accountId)?.displayName ??
                  c.accountId}
              </option>
            ))}
          </select>
        )}
      </div>
      <LineChart
        title={`${name}: units in this match and on average over recent matches`}
        height={200}
        xFormat={tickTime}
        series={[
          {
            name: 'This match',
            color: seriesInk(0, theme),
            points: curve.points.map((p) => ({ x: p.tick, y: p.units })),
          },
          {
            name: `${name}’s average`,
            color: seriesInk(1, theme),
            dashed: true,
            points: curve.points.map((p) => ({ x: p.tick, y: p.averageUnits })),
          },
        ]}
      />
    </>
  );
}

function Verification({ detail }: { detail: MatchDetail }) {
  const v = detail.verificationDetail;
  const seatName = (seat: number) => {
    const p = detail.match.participants.find((x) => x.seat === seat);
    return p ? participantName(p) : `Seat ${seat + 1}`;
  };
  return (
    <div className="card">
      <h2 className="card-title">
        Verification <VerificationBadge match={detail.match} />
      </h2>
      <p style={{ margin: '0 0 6px' }}>{VERDICT_TEXT[detail.match.verification]}</p>
      {v?.reason && <p className="caption">Reason: {v.reason}</p>}
      {v?.ratingNote && <p className="caption">{v.ratingNote}</p>}
      {v?.orderRejections && v.orderRejections.length > 0 && (
        <>
          <div className="caption" style={{ marginTop: 6 }}>
            Orders the server refused (a modified or broken client sends these)
          </div>
          <table className="data">
            <thead>
              <tr>
                <th>Player</th>
                <th className="num">Refused</th>
                <th className="num">Late</th>
                <th className="hide-phone">Reasons</th>
              </tr>
            </thead>
            <tbody>
              {v.orderRejections.map((r) => (
                <tr key={r.seat}>
                  <td>{seatName(r.seat)}</td>
                  <td className="num">{r.rejected}</td>
                  <td className="num">{r.stale}</td>
                  <td className="caption hide-phone">
                    {Object.entries(r.reasons)
                      .map(([k, n]) => `${k.replace(/_/g, ' ')} ×${n}`)
                      .join(', ')}
                    {r.firstRejectedTick !== undefined &&
                      ` · first at ${tickTime(r.firstRejectedTick)}`}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}
    </div>
  );
}

function Replay({ detail }: { detail: MatchDetail }) {
  const replay = detail.artifacts.find((a) => a.kind === 'replay');
  const record = detail.artifacts.find((a) => a.kind === 'record');
  return (
    <div className="card">
      <h2 className="card-title">Replay</h2>
      {replay ? (
        <>
          <p className="caption" style={{ margin: '0 0 8px' }}>
            The server’s verified replay. Open it in the game (Load game → Replays) or watch it
            here.
          </p>
          <div className="toolbar" style={{ margin: 0 }}>
            <a className="btn primary" href={watchUrl(replay.url)} data-testid="watch">
              Watch in browser
            </a>
            <a className="btn" href={replay.url} download>
              Download replay
            </a>
            {record && (
              <a className="btn small" href={record.url} download>
                Match record
              </a>
            )}
          </div>
        </>
      ) : (
        <p className="muted" style={{ margin: 0 }}>
          {detail.match.status === 'running'
            ? 'This match is still being played.'
            : 'The replay is available once the server has verified the match.'}
        </p>
      )}
    </div>
  );
}

function Statistics({ detail }: { detail: MatchDetail }) {
  const count = teamCount(detail);
  const keys = [...new Set(detail.teams.flatMap((t) => Object.keys(t.statistics)))].sort();
  if (keys.length === 0) return null;
  const label = (key: string) => key.replace(/([A-Z])/g, ' $1').toLowerCase();
  return (
    <>
      <h2>Final statistics</h2>
      <TableWrap label="Final statistics">
        <table className="data">
          <thead>
            <tr>
              <th>
                <span className="sr-only">Statistic</span>
              </th>
              {detail.teams.map((t) => (
                <th key={t.team} className="num">
                  <span className="sw" style={{ background: teamColor(t.team, count) }} />{' '}
                  {teamName(detail, t.team)}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>prestige</td>
              {detail.teams.map((t) => (
                <td key={t.team} className="num">
                  {t.prestige}
                </td>
              ))}
            </tr>
            {keys.map((key) => (
              <tr key={key}>
                <td>{label(key)}</td>
                {detail.teams.map((t) => (
                  <td key={t.team} className="num">
                    {t.statistics[key] ?? '–'}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </TableWrap>
    </>
  );
}

export function Match({ id }: { id: string }) {
  const { instance } = useSession();
  const load = useLoad((signal) => api.match(id, signal), [id]);
  return (
    <Loaded load={load}>
      {(detail) => {
        const m = detail.match;
        const kind =
          m.origin === 'queue' ? queueName(instance?.queues, m.queueId, m.queueName) : 'Room match';
        const map = detail.map;
        return (
          <>
            <div className="page-head match-head">
              <GameArt name="swarm" size={72} className="head-art" />
              <div className="grow">
                <h1 data-testid="match-title">
                  {kind} · {map?.title ?? m.mapTitle ?? 'Custom map'}
                </h1>
                <div className="sub">
                  {dateTime(m.endedAt ?? m.startedAt)} · {duration(m.durationTicks)}
                  {m.rated ? ' · rated' : ' · unrated'}
                  {map?.width && map.height ? ` · ${map.width}×${map.height}` : ''}{' '}
                  <StatusBadge match={m} /> <VerificationBadge match={m} />
                </div>
              </div>
              {map?.mapId && (
                <Link className="btn small" to={`/maps/${map.mapId}`}>
                  Map page
                </Link>
              )}
            </div>
            <TeamCards detail={detail} />
            <Participants detail={detail} />
            <div className="grid2" style={{ marginTop: 'var(--sp-4)' }}>
              <Replay detail={detail} />
              <Verification detail={detail} />
            </div>
            <Connection detail={detail} />
            <h2>Timeline</h2>
            <Timelines detail={detail} />
            <Economy detail={detail} />
            <Statistics detail={detail} />
          </>
        );
      }}
    </Loaded>
  );
}
