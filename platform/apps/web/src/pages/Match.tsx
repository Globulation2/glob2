import { useState } from 'react';
import type { MatchDetail, MatchParticipant, TeamTimelinePoint } from '@glob2/protocol';
import { api } from '../api.ts';
import { LineChart } from '../components/LineChart.tsx';
import { Loaded, StatusBadge, VerificationBadge } from '../components/common.tsx';
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
};

function teamName(detail: MatchDetail, team: number): string {
  const members = detail.match.participants.filter((p) => p.team === team);
  return members.length ? members.map(participantName).join(' + ') : `Team ${team + 1}`;
}

function Participants({ detail }: { detail: MatchDetail }) {
  const rows = [...detail.match.participants].sort((a, b) => a.team - b.team || a.seat - b.seat);
  const rejections = new Map(
    (detail.verificationDetail?.orderRejections ?? []).map((r) => [r.seat, r]),
  );
  const diverged = new Set(detail.verificationDetail?.divergedSeats ?? []);
  return (
    <div className="table-wrap">
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
                  <span className="sw" style={{ background: teamColor(p.team) }} />{' '}
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
    </div>
  );
}

function Timelines({ detail }: { detail: MatchDetail }) {
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
        color: teamColor(t.team),
        points: t.timeline.map((p) => ({ x: p.tick, y: pick(p) })),
      }))}
    />
  );
  return (
    <div className="grid3" data-testid="timelines">
      {chart('Units', (p) => p.units)}
      {chart('Buildings', (p) => p.buildings)}
      {chart('Prestige', (p) => p.prestige)}
    </div>
  );
}

function Economy({ detail }: { detail: MatchDetail }) {
  const curves = detail.economy ?? [];
  const [pick, setPick] = useState(0);
  const curve = curves[pick];
  if (!curve) return null;
  const name =
    detail.match.participants.find((p) => p.accountId === curve.accountId)?.displayName ?? 'Player';
  return (
    <>
      <div className="toolbar">
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
            color: 'var(--s1)',
            points: curve.points.map((p) => ({ x: p.tick, y: p.units })),
          },
          {
            name: `${name}’s average`,
            color: 'var(--s2)',
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
      <h3>
        Verification <VerificationBadge match={detail.match} />
      </h3>
      <p style={{ margin: '0 0 6px' }}>{VERDICT_TEXT[detail.match.verification]}</p>
      {v?.reason && <p className="caption">Reason: {v.reason}</p>}
      {v?.ratingNote && <p className="caption">Ratings: {v.ratingNote}</p>}
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
      <h3>Replay</h3>
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
  const keys = [...new Set(detail.teams.flatMap((t) => Object.keys(t.statistics)))].sort();
  if (keys.length === 0) return null;
  const label = (key: string) => key.replace(/([A-Z])/g, ' $1').toLowerCase();
  return (
    <>
      <h2>Final statistics</h2>
      <div className="table-wrap">
        <table className="data">
          <thead>
            <tr>
              <th />
              {detail.teams.map((t) => (
                <th key={t.team} className="num">
                  <span className="sw" style={{ background: teamColor(t.team) }} />{' '}
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
      </div>
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
        const kind = m.origin === 'queue' ? queueName(instance?.queues, m.queueId) : 'Room match';
        const map = detail.map;
        return (
          <>
            <div className="page-head">
              <div className="grow">
                <h1 data-testid="match-title">
                  {kind} · {map?.title ?? m.mapTitle ?? 'Custom map'}
                </h1>
                <div className="caption">
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
            <Participants detail={detail} />
            <div className="grid2" style={{ marginTop: 12 }}>
              <Replay detail={detail} />
              <Verification detail={detail} />
            </div>
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
