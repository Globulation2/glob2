// Small shared pieces: loading/error states, match rows, player links, avatars, map images.
import type { CSSProperties, ReactNode } from 'react';
import type { MatchSummary, PublicAccount } from '@glob2/protocol';
import { ApiError } from '../api.ts';
import { GameArt, type ArtName } from '../art.tsx';
import { avatarColor } from '../colors.ts';
import {
  ago,
  duration,
  initial,
  outcomeLetter,
  participantName,
  queueName,
  seatOf,
  signed,
  teamColor,
  teamCountOf,
} from '../format.ts';
import { Link } from '../router.tsx';
import { useSession, type Load } from '../state.tsx';

/**
 * A table that may scroll sideways on narrow screens. It is a named, focusable
 * region so keyboard users can scroll it too (WCAG 2.1.1; axe
 * scrollable-region-focusable). `stack` turns rows into cards on phones
 * instead (cells need data-label).
 */
export function TableWrap({
  label,
  stack = false,
  children,
}: {
  label: string;
  stack?: boolean;
  children: ReactNode;
}) {
  return (
    <div
      className={`table-wrap${stack ? ' stack' : ''}`}
      role="region"
      aria-label={label}
      tabIndex={0}
    >
      {children}
    </div>
  );
}

export function Loading() {
  return (
    <p className="loading" role="status">
      <span className="glob-spin" aria-hidden="true" /> Loading…
    </p>
  );
}

export function ErrorNotice({ error }: { error: Error }) {
  const notFound = error instanceof ApiError && error.status === 404;
  return (
    <div className="notice error" role="alert">
      {notFound ? 'Not found. It may have been removed, or the link is wrong.' : error.message}
    </div>
  );
}

/** Renders a Load<T>: spinner, error, or `children(data)`. */
export function Loaded<T>({ load, children }: { load: Load<T>; children: (data: T) => ReactNode }) {
  if (load.status === 'loading') return <Loading />;
  if (load.status === 'error') return <ErrorNotice error={load.error} />;
  return <>{children(load.data)}</>;
}

/** An empty list, with a bit of the game's world so it does not feel broken. */
export function Empty({ children, art = 'clearingFlag' }: { children: ReactNode; art?: ArtName }) {
  return (
    <div className="list empty">
      <GameArt name={art} size={72} className="art" />
      {children}
    </div>
  );
}

export function Avatar({
  account,
  size,
}: {
  account: Pick<PublicAccount, 'id' | 'displayName'>;
  size?: 'small' | 'large';
}) {
  return (
    <span
      className={`avatar${size ? ` ${size}` : ''}`}
      style={{ '--av': avatarColor(account.id) } as CSSProperties}
      aria-hidden="true"
    >
      {initial(account.displayName)}
    </span>
  );
}

export function PlayerLink({ account }: { account: Pick<PublicAccount, 'id' | 'displayName'> }) {
  return <Link to={`/players/${account.id}`}>{account.displayName}</Link>;
}

export function VerificationBadge({ match }: { match: MatchSummary }) {
  switch (match.verification) {
    case 'verified':
      return <span className="badge ok">✓ verified</span>;
    case 'pending':
      return match.status === 'ended' ? <span className="badge">checking…</span> : null;
    case 'diverged':
      return <span className="badge bad">diverged</span>;
    case 'unverifiable':
      return <span className="badge warn">unverifiable</span>;
    case 'failed':
      return <span className="badge warn">not checked</span>;
    default:
      return null;
  }
}

export function StatusBadge({ match }: { match: MatchSummary }) {
  if (match.status === 'running') return <span className="badge gold live">live</span>;
  if (match.status === 'starting') return <span className="badge">starting</span>;
  if (match.status === 'cancelled') return <span className="badge">cancelled</span>;
  if (match.endReason === 'aborted') return <span className="badge warn">aborted</span>;
  return null;
}

/** Team swatches of a match, in the colours the game gave the teams. */
export function TeamSwatches({ match }: { match: MatchSummary }) {
  const teams = [...new Set(match.participants.map((p) => p.team))].sort((a, b) => a - b);
  const count = teamCountOf(match.participants);
  return (
    <span className="swatches" aria-hidden="true">
      {teams.slice(0, 4).map((team) => (
        <span key={team} className="sw" style={{ background: teamColor(team, count) }} />
      ))}
    </span>
  );
}

/** One match in a history list, from the point of view of `accountId` when given. */
export function MatchRow({ match, accountId }: { match: MatchSummary; accountId?: string }) {
  const { instance } = useSession();
  const mine = seatOf(match, accountId);
  const result = outcomeLetter(mine?.outcome);
  const others = match.participants.filter((p) => p !== mine);
  const allies = mine ? others.filter((p) => p.team === mine.team) : [];
  const opponents = mine ? others.filter((p) => p.team !== mine.team) : others;
  const vs = opponents.map(participantName).join(', ');
  const title = mine
    ? `${allies.length ? `with ${allies.map(participantName).join(', ')} ` : ''}vs ${vs || 'nobody'}`
    : match.participants.map(participantName).join(' vs ');
  const change = mine?.rating ? mine.rating.after - mine.rating.before : undefined;
  return (
    <Link className="it" to={`/matches/${match.id}`} data-testid="match-row">
      {mine && (
        <span className={result.className}>
          {result.letter}
          <span className="sr-only">
            {result.letter === 'W'
              ? ' (won)'
              : result.letter === 'L'
                ? ' (lost)'
                : result.letter === 'D'
                  ? ' (draw)'
                  : ''}
          </span>
        </span>
      )}
      <TeamSwatches match={match} />
      <div className="grow">
        <div className="ell title">{title}</div>
        <div className="caption ell">
          {match.mapTitle ?? 'Custom map'} · {ago(match.endedAt ?? match.startedAt)}
        </div>
      </div>
      <span className="caption hide-phone" style={{ width: 120 }}>
        {match.origin === 'queue' ? queueName(instance?.queues, match.queueId) : 'Room'}
      </span>
      <span className="caption num hide-phone" style={{ width: 70 }}>
        {duration(match.durationTicks)}
      </span>
      <span className="num" style={{ minWidth: 72 }}>
        {change !== undefined ? (
          <span className={change >= 0 ? 'up' : 'dn'}>{signed(change)}</span>
        ) : (
          <>
            <StatusBadge match={match} /> <VerificationBadge match={match} />
          </>
        )}
      </span>
    </Link>
  );
}

export function MatchListView({
  matches,
  accountId,
  empty = 'No matches yet.',
}: {
  matches: MatchSummary[];
  accountId?: string;
  empty?: string;
}) {
  if (matches.length === 0) return <Empty art="warFlag">{empty}</Empty>;
  return (
    <div className="list">
      {matches.map((m) => (
        <MatchRow key={m.id} match={m} {...(accountId ? { accountId } : {})} />
      ))}
    </div>
  );
}

export function MapImage({
  src,
  alt,
  size,
}: {
  src: string | undefined;
  alt: string;
  size?: number;
}) {
  const style = size ? { width: size, height: size } : undefined;
  return src ? (
    <img
      className="mapimg"
      src={src}
      alt={alt}
      loading="lazy"
      decoding="async"
      style={style}
      width={size ?? 384}
      height={size ?? 384}
    />
  ) : (
    <div className="mapimg none" role="img" aria-label={`${alt} (no preview)`} style={style} />
  );
}
