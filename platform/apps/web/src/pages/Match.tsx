import { statusLabel } from '../i18n.tsx';

import { t as translate, useLocale, RichMessage } from '../i18n.tsx';
import { aiHref } from '../playerLinks.ts';
import { versionKey } from '../format.ts';
import { MatchSkinLooks } from '../skins/Reporting.tsx';
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
  return members.length
    ? members.map(participantName).join(' + ')
    : translate('Team {value0}', { value0: team + 1 });
}

function TeamCards({ detail }: { detail: MatchDetail }) {
  useLocale();
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
              {outcome
                ? won
                  ? translate('Winner')
                  : outcome === 'draw'
                    ? translate('Draw')
                    : outcome
                : translate('Team')}
            </div>
            <div style={{ fontWeight: 750, fontSize: 'var(--text-lg)' }}>
              {teamName(detail, team)}
            </div>
            {stats && (
              <div className="caption">
                <RichMessage source={'Prestige {slot0}'} slots={{ slot0: stats.prestige }} />
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}

function Participants({ detail }: { detail: MatchDetail }) {
  useLocale();
  const count = teamCount(detail);
  const rows = [...detail.match.participants].sort((a, b) => a.team - b.team || a.seat - b.seat);
  const rejections = new Map(
    (detail.verificationDetail?.orderRejections ?? []).map((r) => [r.seat, r]),
  );
  const diverged = new Set(detail.verificationDetail?.divergedSeats ?? []);
  return (
    <TableWrap label={translate('Players and results')}>
      <table className="data">
        <thead>
          <tr>
            <th>{translate('Player')}</th>
            <th>{translate('Result')}</th>
            <th className="num">{translate('Rating')}</th>
            <th className="num hide-phone">{translate('Change')}</th>
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
                    <Link to={`/players/${p.accountId}`}>
                      <bdi dir="auto">{p.displayName}</bdi>
                    </Link>
                  ) : p.kind === 'ai' ? (
                    <>
                      <Link to={aiHref(p.ai ?? 'none', versionKey(detail.match.simVersion))}>
                        {aiName(p.ai)}
                      </Link>{' '}
                      <span className="badge">{translate('AI')}</span>
                    </>
                  ) : (
                    p.displayName
                  )}
                  {p.disconnects > 0 && (
                    <span className="caption">
                      <RichMessage
                        source={' · {slot0} disconnects'}
                        singular={' · {slot0} disconnect'}
                        count={p.disconnects}
                        slots={{ slot0: p.disconnects }}
                      />
                    </span>
                  )}
                  {diverged.has(p.seat) && (
                    <span className="badge bad"> {translate(' diverged')}</span>
                  )}
                  {rejections.has(p.seat) && (
                    <span className="badge warn"> {translate(' refused orders')}</span>
                  )}
                </td>
                <td>{p.outcome ? statusLabel(p.outcome) : '–'}</td>
                <td className="num">
                  {r ? (
                    <>
                      {rating(r.before)} {translate(' → ')}
                      {rating(r.after)}
                      {r.provisional && (
                        <span className="caption"> {translate(' (provisional)')}</span>
                      )}
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
  useLocale();
  if (!spread) return <>{translate('–')}</>;
  return (
    <>
      {formatConnectionValue(metric, spread.p50)}
      <span className="caption">
        <RichMessage
          source={' · {slot0}'}
          slots={{ slot0: translate(QUALITY_LABEL[rateConnection(metric, spread.p50)]) }}
        />
      </span>
      <br />
      <span className="caption">
        <RichMessage
          source={'95%: {slot0}'}
          slots={{ slot0: formatConnectionValue(metric, spread.p95) }}
        />
      </span>
    </>
  );
}

/** "Good under 150 ms, Fair under 300 ms, Poor from 300 ms." */
function limits(metric: ConnectionMetric): string {
  const t = CONNECTION_METRICS[metric];
  const v = (ms: number) => formatConnectionValue(metric, ms).replace(/\.0 s$/, ' s');
  return translate('Good under {value0}, fair under {value1}, poor from {value2}.', {
    value0: v(t.fairMs),
    value1: v(t.poorMs),
    value2: v(t.poorMs),
  });
}

function seconds(ms: number): string {
  if (ms === 0) return '–';
  return ms < 10_000 ? `${(ms / 1000).toFixed(1)} s` : `${Math.round(ms / 1000)} s`;
}

/** Each human player's connection as the relay measured it (MatchDetail.network). */
function Connection({ detail }: { detail: MatchDetail }) {
  useLocale();
  const rows = detail.network ?? [];
  if (rows.length === 0) return null;
  const player = (seat: number) => {
    const p = detail.match.participants.find((x) => x.seat === seat);
    return p ? participantName(p) : translate('Seat {value0}', { value0: seat + 1 });
  };
  const count = teamCount(detail);
  const team = (seat: number) => detail.match.participants.find((x) => x.seat === seat)?.team;
  return (
    <section className="net-quality" data-testid="network">
      <h2>{translate('Connection')}</h2>
      <TableWrap label={translate('Connection quality per player')} stack>
        <table className="data">
          <thead>
            <tr>
              <th>{translate('Player')}</th>
              <th>{translate('Quality')}</th>
              <th className="num">{translate('Ping')}</th>
              <th className="num">{translate('Behind')}</th>
              <th className="num">{translate('Disconnects')}</th>
              <th className="num">{translate('Offline')}</th>
              <th className="num">{translate('Delayed orders')}</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((n) => {
              const t = team(n.seat);
              return (
                <tr key={n.seat} data-testid="network-row">
                  <td data-label={translate('Player')} className="stack-head">
                    {t !== undefined && (
                      <>
                        <span className="sw" style={{ background: teamColor(t, count) }} />{' '}
                      </>
                    )}
                    {player(n.seat)}
                    {n.rejoins > 0 && (
                      <>
                        {' '}
                        <span className="badge warn">{translate('resynced')}</span>
                      </>
                    )}
                    {n.leftBy === 'grace' && (
                      <>
                        {' '}
                        <span className="badge bad">{translate('dropped')}</span>
                      </>
                    )}
                  </td>
                  <td data-label={translate('Quality')}>
                    <span className={QUALITY_BADGE[n.quality]}>
                      {translate(QUALITY_LABEL[n.quality])}
                    </span>
                  </td>
                  <td className="num" data-label={translate('Ping')}>
                    <Measured metric="ping" spread={n.rttMs} />
                  </td>
                  <td className="num" data-label={translate('Behind')}>
                    <Measured metric="behind" spread={n.lagMs} />
                  </td>
                  <td className="num" data-label={translate('Disconnects')}>
                    {n.disconnects}
                  </td>
                  <td className="num" data-label={translate('Offline')}>
                    {seconds(n.offlineMs)}
                  </td>
                  <td className="num" data-label={translate('Delayed orders')}>
                    {n.ordersDeferred}
                    <span className="caption">
                      <RichMessage source={' / {slot0}'} slots={{ slot0: n.ordersSequenced }} />
                    </span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </TableWrap>
      <p className="caption" data-testid="network-legend">
        <RichMessage
          source={
            'Measured by the relay; typical value first, then the 95th percentile. Ping: round trip between the player and the relay. {slot0} Behind: how far the player’s game ran behind the match clock, their input delay included. {slot1} Quality is the worst of these and of reconnects, time offline and delayed orders. Offline: time disconnected before reconnecting. Delayed orders: orders that ran a tick later than asked.'
          }
          slots={{ slot0: limits('ping'), slot1: limits('behind') }}
        />
      </p>
    </section>
  );
}

function Timelines({ detail }: { detail: MatchDetail }) {
  useLocale();
  const { theme } = useTheme();
  const count = teamCount(detail);
  const teams = detail.teams.filter((t) => t.timeline.length > 0);
  if (teams.length === 0) {
    return (
      <p className="muted">
        {detail.match.verification === 'pending'
          ? translate('Charts appear once the server has replayed the match.')
          : translate('No timeline was recorded for this match.')}
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
      {chart(translate('Units'), (p) => p.units)}
      {chart(translate('Buildings'), (p) => p.buildings)}
      {chart(translate('Prestige '), (p) => p.prestige)}
    </div>
  );
}

function Economy({ detail }: { detail: MatchDetail }) {
  useLocale();
  const { theme } = useTheme();
  const curves = detail.economy ?? [];
  const [pick, setPick] = useState(0);
  const curve = curves[pick];
  if (!curve) return null;
  // Until the player has other matches the "average" is this match again:
  // two identical lines that say nothing, so the section waits for history.
  if (!curves.some((c) => c.points.some((p) => p.gamesAtTick > 1))) return null;
  const name =
    detail.match.participants.find((p) => p.accountId === curve.accountId)?.displayName ?? 'Player';
  return (
    <>
      <div className="toolbar" style={{ marginTop: 'var(--sp-6)' }}>
        <h2 className="grow" style={{ margin: 0 }}>
          {translate('Economy against each player’s average')}
        </h2>
        {curves.length > 1 && (
          <select
            aria-label={translate('Player')}
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
        title={translate('{value0}: units in this match and on average over recent matches', {
          value0: name,
        })}
        height={200}
        xFormat={tickTime}
        series={[
          {
            name: translate('This match'),
            color: seriesInk(0, theme),
            points: curve.points.map((p) => ({ x: p.tick, y: p.units })),
          },
          {
            name: translate('{value0}’s average', { value0: name }),
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
  useLocale();
  const v = detail.verificationDetail;
  const seatName = (seat: number) => {
    const p = detail.match.participants.find((x) => x.seat === seat);
    return p ? participantName(p) : translate('Seat {value0}', { value0: seat + 1 });
  };
  return (
    <div className="card">
      <h2 className="card-title">
        {translate('Verification ')}
        <VerificationBadge match={detail.match} />
      </h2>
      <p style={{ margin: '0 0 6px' }}>{VERDICT_TEXT[detail.match.verification]}</p>
      {v?.reason && (
        <p className="caption">
          <RichMessage source={'Reason: {slot0}'} slots={{ slot0: v.reason }} />
        </p>
      )}
      {v?.ratingNote && <p className="caption">{v.ratingNote}</p>}
      {v?.orderRejections && v.orderRejections.length > 0 && (
        <>
          <div className="caption" style={{ marginTop: 6 }}>
            {translate('Orders the server refused (a modified or broken client sends these)')}
          </div>
          <table className="data">
            <thead>
              <tr>
                <th>{translate('Player')}</th>
                <th className="num">{translate('Refused')}</th>
                <th className="num">{translate('Late')}</th>
                <th className="hide-phone">{translate('Reasons')}</th>
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
                      translate(' · first at {value0}', { value0: tickTime(r.firstRejectedTick) })}
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
  useLocale();
  const replay = detail.artifacts.find((a) => a.kind === 'replay');
  const record = detail.artifacts.find((a) => a.kind === 'record');
  return (
    <div className="card">
      <h2 className="card-title">{translate('Replay')}</h2>
      {replay ? (
        <>
          <p className="caption" style={{ margin: '0 0 8px' }}>
            {translate(
              'The server’s verified replay. Open it in the game (Load game → Replays) or watch it here.',
            )}
          </p>
          <div className="toolbar" style={{ margin: 0 }}>
            <a className="btn primary" href={watchUrl(replay.url)} data-testid="watch">
              {translate('Watch in browser')}
            </a>
            <a className="btn" href={replay.url} download>
              {translate('Download replay')}
            </a>
            {record && (
              <a className="btn small" href={record.url} download>
                {translate('Match record')}
              </a>
            )}
          </div>
        </>
      ) : (
        <p className="muted" style={{ margin: 0 }}>
          {detail.match.status === 'running'
            ? translate('This match is still being played.')
            : translate('The replay is available once the server has verified the match.')}
        </p>
      )}
    </div>
  );
}

/** Final statistics in reading order, with names a player recognises. */
const STATISTICS: { key: string; label: string; yesNo?: boolean; sameAs?: string }[] = [
  { key: 'alive', label: 'Colony still standing', yesNo: true },
  { key: 'units', label: 'Globs' },
  { key: 'totalUnits', label: 'Globs (all)', sameAs: 'units' },
  { key: 'workers', label: 'Workers' },
  { key: 'explorers', label: 'Explorers' },
  { key: 'warriors', label: 'Warriors' },
  { key: 'buildings', label: 'Buildings' },
  { key: 'totalBuildings', label: 'Finished buildings', sameAs: 'buildings' },
  { key: 'sites', label: 'Buildings under construction' },
  { key: 'food', label: 'Food in inns' },
  { key: 'foodCapacity', label: 'Inn capacity' },
  { key: 'needFood', label: 'Hungry globs' },
  { key: 'totalHp', label: 'Total health' },
  { key: 'warriorHp', label: 'Warrior health' },
  { key: 'warriorAttack', label: 'Warrior attack' },
  { key: 'totalAttackPower', label: 'Attack power' },
  { key: 'totalDefensePower', label: 'Defence power' },
];

/** "needFood" → "Need food", for statistics this page has no name for. */
export function statisticLabel(key: string): string {
  const words = key
    .replace(/_/g, ' ')
    .replace(/([a-z])([A-Z])/g, '$1 $2')
    .toLowerCase()
    .trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
}

function Statistics({ detail }: { detail: MatchDetail }) {
  useLocale();
  const count = teamCount(detail);
  const present = new Set(detail.teams.flatMap((t) => Object.keys(t.statistics)));
  if (present.size === 0) return null;
  const known = new Set(STATISTICS.map((s) => s.key));
  const rows = [
    ...STATISTICS.filter(
      (s) =>
        present.has(s.key) &&
        // Skip a total that repeats another row for every team.
        !(
          s.sameAs &&
          detail.teams.every((t) => t.statistics[s.key] === t.statistics[s.sameAs ?? ''])
        ),
    ),
    ...[...present]
      .filter((key) => !known.has(key))
      .sort()
      .map((key) => ({ key, label: statisticLabel(key), yesNo: false })),
  ];
  const value = (row: { key: string; yesNo?: boolean }, v: number | undefined) =>
    v === undefined ? '–' : row.yesNo ? (v ? translate('Yes') : 'No') : v;
  return (
    <>
      <h2>{translate('Final statistics')}</h2>
      <TableWrap label={translate('Final statistics')}>
        <table className="data">
          <thead>
            <tr>
              <th>
                <span className="sr-only">{translate('Statistic')}</span>
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
              <td>{translate('Prestige ')}</td>
              {detail.teams.map((t) => (
                <td key={t.team} className="num">
                  {t.prestige}
                </td>
              ))}
            </tr>
            {rows.map((row) => (
              <tr key={row.key}>
                <td>{translate(row.label)}</td>
                {detail.teams.map((t) => (
                  <td key={t.team} className="num">
                    {value(row, t.statistics[row.key])}
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
  useLocale();
  const { instance } = useSession();
  const load = useLoad((signal) => api.match(id, signal), [id]);
  return (
    <Loaded load={load} page="Match">
      {(detail) => {
        const m = detail.match;
        const kind =
          m.origin === 'queue'
            ? queueName(instance?.queues, m.queueId, m.queueName)
            : translate('Room match');
        const map = detail.map;
        return (
          <>
            <div className="page-head match-head">
              <GameArt name="warrior" size={72} className="head-art" />
              <div className="grow">
                <h1 data-testid="match-title">
                  <RichMessage
                    source={'{slot0} · {slot1}'}
                    slots={{
                      slot0: kind,
                      slot1: map?.title ?? m.mapTitle ?? translate('Custom map'),
                    }}
                  />
                </h1>
                <div className="sub">
                  {dateTime(m.endedAt ?? m.startedAt)} {translate(' · ')}
                  {duration(m.durationTicks)}
                  {m.rated ? translate(' · rated') : translate(' · unrated')}
                  {map?.width && map.height ? ` · ${map.width}×${map.height}` : ''}{' '}
                  <StatusBadge match={m} /> <VerificationBadge match={m} />
                </div>
              </div>
              {map?.mapId && (
                <Link className="btn small" to={`/maps/${map.mapId}`}>
                  {translate('Map page')}
                </Link>
              )}
            </div>
            <TeamCards detail={detail} />
            <Replay detail={detail} />
            <Participants detail={detail} />
            <MatchSkinLooks matchId={id} />
            <h2>{translate('Timeline')}</h2>
            <Timelines detail={detail} />
            <Economy detail={detail} />
            <Statistics detail={detail} />
            <details className="match-diagnostics">
              <summary>{translate('Verification and connection details')}</summary>
              <Verification detail={detail} />
              <Connection detail={detail} />
            </details>
          </>
        );
      }}
    </Loaded>
  );
}
