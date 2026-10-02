// Display helpers shared by pages.
import type { MatchParticipant, MatchSummary, QueueInfo } from '@glob2/protocol';

/** Engine ticks per second (the relay clock). */
export const TICKS_PER_SECOND = 25;

export function duration(ticks: number | undefined): string {
  if (ticks === undefined) return '–';
  const minutes = Math.round(ticks / TICKS_PER_SECOND / 60);
  if (minutes < 60) return `${minutes} min`;
  return `${Math.floor(minutes / 60)} h ${minutes % 60} min`;
}

export function tickTime(tick: number): string {
  const seconds = Math.round(tick / TICKS_PER_SECOND);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' });
const dateTimeFormat = new Intl.DateTimeFormat(undefined, {
  dateStyle: 'medium',
  timeStyle: 'short',
});

export function date(iso: string | undefined): string {
  return iso ? dateFormat.format(new Date(iso)) : '–';
}

export function dateTime(iso: string | undefined): string {
  return iso ? dateTimeFormat.format(new Date(iso)) : '–';
}

/** "today 14:02", "yesterday", "3 days ago", else the date. */
export function ago(iso: string | undefined, now = Date.now()): string {
  if (!iso) return '–';
  const then = new Date(iso);
  const days = Math.floor((now - then.getTime()) / 86_400_000);
  if (days <= 0) {
    return `today ${then.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })}`;
  }
  if (days === 1) return 'yesterday';
  if (days < 7) return `${days} days ago`;
  return dateFormat.format(then);
}

export function rating(value: number): string {
  return String(Math.round(value));
}

export function signed(value: number): string {
  const rounded = Math.round(value);
  if (rounded > 0) return `+${rounded}`;
  if (rounded < 0) return `−${Math.abs(rounded)}`;
  return '±0';
}

export function percent(value: number): string {
  return `${Math.round(value * 100)} %`;
}

export function queueName(queues: readonly QueueInfo[] | undefined, id: string | undefined) {
  if (!id) return 'Room';
  if (id === 'room') return 'Rooms';
  return queues?.find((q) => q.id === id)?.name ?? id;
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
  return { letter: '–', className: 'res' };
}

/** The series colour of team `index` (CSS variables --s0..--s5). */
export function teamColor(index: number): string {
  return `var(--s${index % 6})`;
}

export function initial(name: string): string {
  return (name.trim().charAt(0) || '?').toUpperCase();
}
