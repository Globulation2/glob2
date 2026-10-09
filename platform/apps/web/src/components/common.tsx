import { t, useLocale, RichMessage } from '../i18n.tsx';
// Small shared pieces: loading/error states, match rows, player links, avatars, map images.
import { useState, type CSSProperties, type ReactNode } from 'react';
import type { MatchSummary, PublicAccount } from '@glob2/protocol';
import { ApiError } from '../api.ts';
import { GameArt, type ArtName } from '../art.tsx';
import { avatarColor } from '../colors.ts';
import { Icon } from '../icons.tsx';
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
  useLocale();
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
  useLocale();
  return (
    <p className="loading" role="status">
      <span className="glob-spin" aria-hidden="true" /> {t(' Loading…')}
    </p>
  );
}

export function ErrorNotice({ error }: { error: Error }) {
  useLocale();
  const notFound = error instanceof ApiError && error.status === 404;
  return (
    <div className="notice error" role="alert">
      {notFound ? t('Not found. It may have been removed, or the link is wrong.') : error.message}
    </div>
  );
}

/**
 * Renders a Load<T>: spinner, error, or `children(data)`. A page-level load
 * passes `page` (what the page shows, "Match") so a missing item still gets a
 * page heading for screen readers and the tab title.
 */
export function Loaded<T>({
  load,
  children,
  page,
}: {
  load: Load<T>;
  children: (data: T) => ReactNode;
  page?: string;
}) {
  useLocale();
  if (load.status === 'loading') return <Loading />;
  if (load.status === 'error') {
    if (!page) return <ErrorNotice error={load.error} />;
    const notFound = load.error instanceof ApiError && load.error.status === 404;
    return (
      <>
        <h1>
          {notFound
            ? t('{value0} not found', { value0: t(page) })
            : t('Could not load this {value0}', { value0: t(page) })}
        </h1>
        <ErrorNotice error={load.error} />
        <p>
          <Link to="/">{t('Go to the home page')}</Link>
        </p>
      </>
    );
  }
  return <>{children(load.data)}</>;
}

/** An empty list, with a bit of the game's world so it does not feel broken. */
export function Empty({ children, art = 'clearingFlag' }: { children: ReactNode; art?: ArtName }) {
  useLocale();
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
  account: Pick<PublicAccount, 'id' | 'displayName' | 'avatarUrl'>;
  size?: 'small' | 'large';
}) {
  useLocale();
  const [failed, setFailed] = useState<string>();
  const src = account.avatarUrl ?? `/api/v1/accounts/${account.id}/avatar`;
  return (
    <span
      className={`avatar${size ? ` ${size}` : ''}`}
      style={{ '--av': avatarColor(account.id) } as CSSProperties}
      aria-hidden="true"
    >
      {initial(account.displayName)}
      {failed !== src && <img src={src} alt="" onError={() => setFailed(src)} loading="lazy" />}
    </span>
  );
}

const AI_ICON_SIZES = { small: 18, medium: 30, large: 50 };

/** An AI player's stand-in for an avatar. */
export function AiMark({ size }: { size?: 'small' | 'large' }) {
  useLocale();
  return (
    <span className={`avatar ai-avatar${size ? ` ${size}` : ''}`} aria-hidden="true">
      <Icon name="robot" size={AI_ICON_SIZES[size ?? 'medium']} />
    </span>
  );
}

export function PlayerLink({ account }: { account: Pick<PublicAccount, 'id' | 'displayName'> }) {
  useLocale();
  return (
    <Link to={`/players/${account.id}`}>
      <bdi dir="auto">{account.displayName}</bdi>
    </Link>
  );
}

export function VerificationBadge({ match }: { match: MatchSummary }) {
  useLocale();
  switch (match.verification) {
    case 'verified':
      return <span className="badge ok">{t('✓ verified')}</span>;
    case 'pending':
      return match.status === 'ended' ? <span className="badge">{t('checking…')}</span> : null;
    case 'diverged':
      return <span className="badge bad">{t('diverged')}</span>;
    case 'unverifiable':
      return <span className="badge warn">{t('unverifiable')}</span>;
    case 'failed':
      return <span className="badge warn">{t('not checked')}</span>;
    default:
      return null;
  }
}

export function StatusBadge({ match }: { match: MatchSummary }) {
  useLocale();
  if (match.status === 'running') return <span className="badge gold live">{t('live')}</span>;
  if (match.status === 'starting') return <span className="badge">{t('starting')}</span>;
  if (match.status === 'cancelled') return <span className="badge">{t('cancelled')}</span>;
  if (match.endReason === 'aborted') return <span className="badge warn">{t('aborted')}</span>;
  return null;
}

/** Team swatches of a match, in the colours the game gave the teams. */
export function TeamSwatches({ match }: { match: MatchSummary }) {
  useLocale();
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
export function MatchRow({
  match,
  accountId,
  aiId,
}: {
  match: MatchSummary;
  accountId?: string;
  aiId?: string;
}) {
  useLocale();
  const { instance } = useSession();
  const mine = aiId
    ? match.participants.find((p) => p.kind === 'ai' && p.ai === aiId)
    : seatOf(match, accountId);
  const result = outcomeLetter(mine?.outcome);
  const others = match.participants.filter((p) => p !== mine);
  const allies = mine ? others.filter((p) => p.team === mine.team) : [];
  const opponents = mine ? others.filter((p) => p.team !== mine.team) : others;
  const vs = opponents.map(participantName).join(', ');
  const title = mine
    ? t('{value0}vs {value1}', {
        value0: allies.length
          ? t('with {value0} ', { value0: allies.map(participantName).join(', ') })
          : '',
        value1: vs || 'nobody',
      })
    : match.participants.map(participantName).join(' vs ');
  const change = mine?.rating ? mine.rating.after - mine.rating.before : undefined;
  return (
    <Link className="it" to={`/matches/${match.id}`} data-testid="match-row">
      {mine && (
        <span className={result.className}>
          {result.letter}
          <span className="sr-only">
            {result.letter === 'W'
              ? t(' (won)')
              : result.letter === 'L'
                ? t(' (lost)')
                : result.letter === 'D'
                  ? t(' (draw)')
                  : ''}
          </span>
        </span>
      )}
      <TeamSwatches match={match} />
      <div className="grow">
        <div className="ell title">{title}</div>
        <div className="caption ell">
          <RichMessage
            source={'{slot0} · {slot1}'}
            slots={{
              slot0: match.mapTitle ?? t('Custom map'),
              slot1: ago(match.endedAt ?? match.startedAt),
            }}
          />
        </div>
      </div>
      <span className="caption hide-phone" style={{ width: 120 }}>
        {match.origin === 'queue'
          ? queueName(instance?.queues, match.queueId, match.queueName)
          : t('Room')}
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
  aiId,
  empty = t('No matches yet.'),
}: {
  matches: MatchSummary[];
  accountId?: string;
  aiId?: string;
  empty?: string;
}) {
  useLocale();
  if (matches.length === 0) return <Empty art="warFlag">{empty}</Empty>;
  return (
    <div className="list">
      {matches.map((m) => (
        <MatchRow
          key={m.id}
          match={m}
          {...(accountId ? { accountId } : {})}
          {...(aiId ? { aiId } : {})}
        />
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
  useLocale();
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
    <div
      className="mapimg none"
      role="img"
      aria-label={t('{value0} (no preview)', { value0: alt })}
      style={style}
    />
  );
}
