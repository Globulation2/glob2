import { getLocale } from './messages.ts';
import { t } from './messages.ts';
// Display helpers shared by pages.
import {
  TICKS_PER_SECOND,
  type MatchParticipant,
  type MatchSummary,
  type QueueInfo,
} from '@glob2/protocol';
import { gameTeamColor } from './colors.ts';

/** Engine ticks per second (the relay clock). */
export { TICKS_PER_SECOND };

export function duration(ticks: number | undefined): string {
  if (ticks === undefined) return '–';
  const seconds = Math.round(ticks / TICKS_PER_SECOND);
  if (seconds < 60)
    return new Intl.NumberFormat(getLocale(), {
      style: 'unit',
      unit: 'second',
      unitDisplay: 'short',
    }).format(seconds);
  const minutes = Math.round(seconds / 60);
  if (minutes < 60)
    return new Intl.NumberFormat(getLocale(), {
      style: 'unit',
      unit: 'minute',
      unitDisplay: 'short',
    }).format(minutes);
  return t('{hours} {minutes}', {
    hours: new Intl.NumberFormat(getLocale(), {
      style: 'unit',
      unit: 'hour',
      unitDisplay: 'short',
    }).format(Math.floor(minutes / 60)),
    minutes: new Intl.NumberFormat(getLocale(), {
      style: 'unit',
      unit: 'minute',
      unitDisplay: 'short',
    }).format(minutes % 60),
  });
}

export function tickTime(tick: number): string {
  const seconds = Math.round(tick / TICKS_PER_SECOND);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

const dateFormat = () => new Intl.DateTimeFormat(getLocale(), { dateStyle: 'medium' });
const dateTimeFormat = () =>
  new Intl.DateTimeFormat(getLocale(), {
    dateStyle: 'medium',
    timeStyle: 'short',
  });

export function date(iso: string | undefined): string {
  return iso ? dateFormat().format(new Date(iso)) : '–';
}

export function dateTime(iso: string | undefined): string {
  return iso ? dateTimeFormat().format(new Date(iso)) : '–';
}

/** "today 14:02", "yesterday", "3 days ago", else the date. */
export function ago(iso: string | undefined, now = Date.now()): string {
  if (!iso) return '–';
  const then = new Date(iso);
  const days = Math.floor((now - then.getTime()) / 86_400_000);
  if (days <= 0) {
    return t('today {time}', {
      time: then.toLocaleTimeString(getLocale(), { hour: '2-digit', minute: '2-digit' }),
    });
  }
  if (days === 1)
    return new Intl.RelativeTimeFormat(getLocale(), { numeric: 'auto' }).format(-1, 'day');
  if (days < 7)
    return new Intl.RelativeTimeFormat(getLocale(), { numeric: 'auto' }).format(-days, 'day');
  return dateFormat().format(then);
}

export function rating(value: number): string {
  return new Intl.NumberFormat(getLocale(), { useGrouping: false }).format(Math.round(value));
}

export function signed(value: number): string {
  const rounded = Math.round(value);
  if (rounded > 0) return `+${rounded}`;
  if (rounded < 0) return `−${Math.abs(rounded)}`;
  return '±0';
}

export function percent(value: number): string {
  return new Intl.NumberFormat(getLocale(), { style: 'percent', maximumFractionDigits: 0 }).format(
    value,
  );
}

/**
 * A queue as players know it: the name the server sent with the match, else the
 * instance's queue list, else a readable form of the id ("casual-1v1" → "Casual 1v1").
 */
export function queueName(
  queues: readonly QueueInfo[] | undefined,
  id: string | undefined,
  known?: string,
) {
  if (known) return known;
  if (!id) return t('Room');
  if (id === 'room') return t('Rooms');
  return queues?.find((q) => q.id === id)?.name ?? readableId(id);
}

function readableId(id: string): string {
  const words = id.split('-').filter(Boolean);
  return words.map((w, i) => (i === 0 ? w.charAt(0).toUpperCase() + w.slice(1) : w)).join(' ');
}

const AI_NAMES: Record<string, string> = {
  none: 'Nobody',
  numbi: 'Numbi',
  castor: 'Castor',
  warrush: 'Warrush',
  reachtoinfinity: 'Reach to Infinity',
  nicowar: 'Nicowar',
  toubib: 'Toubib',
  maxima: 'Maxima',
  cortex: 'Cortex',
  cabino: 'Cabino',
  econo: 'Econo',
};

export function aiName(id: string | undefined): string {
  if (!id) return 'AI';
  return AI_NAMES[id] ?? id.charAt(0).toUpperCase() + id.slice(1);
}

export function participantName(p: MatchParticipant): string {
  return p.kind === 'ai' ? `${aiName(p.ai)} (AI)` : p.displayName;
}

/** The viewer's seat in a match, if they played it. */
export function seatOf(match: MatchSummary, accountId: string | undefined) {
  return accountId ? match.participants.find((p) => p.accountId === accountId) : undefined;
}

export function outcomeLetter(outcome: string | undefined): { letter: string; className: string } {
  if (outcome === 'won') return { letter: 'W', className: 'res w' };
  if (outcome === 'lost' || outcome === 'abandoned') return { letter: 'L', className: 'res l' };
  if (outcome === 'draw') return { letter: 'D', className: 'res d' };
  return { letter: '–', className: 'res' };
}

/**
 * The in-game colour of team `index` on a map of `teamCount` teams (the
 * engine spreads team hues evenly around the colour wheel).
 */
export function teamColor(index: number, teamCount: number): string {
  return gameTeamColor(index, teamCount);
}

/** Number of teams a match summary shows: its highest team index plus one. */
export function teamCountOf(participants: readonly { team: number }[]): number {
  return participants.reduce((n, p) => Math.max(n, p.team + 1), 1);
}

export function initial(name: string): string {
  return (name.trim().charAt(0) || '?').toUpperCase();
}

/** A sim version's key (same as simVersionKey in @glob2/protocol, without loading the schemas). */
export function versionKey(v: { versionMinor: number; netProtocol: number; dataHash: string }) {
  return `${v.versionMinor}-${v.netProtocol}-${v.dataHash}`;
}
