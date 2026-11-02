// Pure matchmaking decisions: rating windows, region choice, group formation,
// side balance and AI backfill. The Matchmaker feeds it one queue's waiting
// tickets of one sim version and turns the plan into proposals.
import { QUEUE_MODES, modeSeats, type ResolvedQueue } from '@glob2/core';
import type { RatedAi } from '../ratings/entities.ts';
import { matchSkill } from '../ratings/scale.ts';

export interface RegionRtt {
  region: string;
  rttMs: number;
}

export interface WaitingTicket {
  id: string;
  accountId: string;
  /** Queue position: tickets keep it when a proposal they were in is cancelled. */
  createdAt: Date;
  mu: number;
  sigma: number;
  regions: readonly RegionRtt[];
  allowAi: boolean;
}

export interface AiCandidate {
  ai: RatedAi;
  entityId: string;
  mu: number;
  sigma: number;
}

export type GroupMember =
  { kind: 'human'; ticket: WaitingTicket } | { kind: 'ai'; candidate: AiCandidate };

export interface PlannedGroup {
  /** Members by side: sides[0] and sides[1]. */
  sides: GroupMember[][];
  region: string | null;
  backfilled: boolean;
}

export function waitedSeconds(ticket: WaitingTicket, now: Date): number {
  return Math.max(0, (now.getTime() - ticket.createdAt.getTime()) / 1000);
}

/** Accepted skill difference (display points) after waiting `seconds`. */
export function ratingWindow(queue: ResolvedQueue, seconds: number): number {
  const w = queue.ratingWindow;
  return Math.min(w.max, w.initial + w.perSecond * seconds);
}

export function backfillDue(queue: ResolvedQueue, ticket: WaitingTicket, now: Date): boolean {
  return (
    queue.aiBackfillSeconds !== undefined &&
    ticket.allowAi &&
    waitedSeconds(ticket, now) >= queue.aiBackfillSeconds
  );
}

/** When the ticket becomes eligible for AI backfill, if ever. */
export function backfillAt(queue: ResolvedQueue, ticket: WaitingTicket): Date | undefined {
  if (queue.aiBackfillSeconds === undefined || !ticket.allowAi) return undefined;
  return new Date(ticket.createdAt.getTime() + queue.aiBackfillSeconds * 1000);
}

/**
 * The relay region for a group: never fails. Candidates are the regions any
 * member probed (the relays that exist); the best one is reported by the most
 * members, then has the lowest worst round trip, then the lowest name.
 * `worst` is that worst round trip, or Infinity when some member with probes
 * did not reach the region (no shared region). Members without probes fit
 * anywhere; with no probes at all the region is null (any relay) and worst 0.
 */
export function chooseRegion(members: readonly { regions: readonly RegionRtt[] }[]): {
  region: string | null;
  worst: number;
} {
  const measured = members.filter((m) => m.regions.length > 0);
  const regions = [...new Set(measured.flatMap((m) => m.regions.map((r) => r.region)))];
  let best: { region: string; missing: number; worst: number } | undefined;
  for (const region of regions) {
    let missing = 0;
    let worst = 0;
    for (const member of measured) {
      const probe = member.regions.find((r) => r.region === region);
      if (probe) worst = Math.max(worst, probe.rttMs);
      else missing++;
    }
    if (
      !best ||
      missing < best.missing ||
      (missing === best.missing &&
        (worst < best.worst || (worst === best.worst && region < best.region)))
    ) {
      best = { region, missing, worst };
    }
  }
  if (!best) return { region: null, worst: 0 };
  return { region: best.region, worst: best.missing > 0 ? Infinity : best.worst };
}

/** Worst relay round trip a ticket accepts for a pairing after waiting `seconds` (soft). */
export function rttTolerance(queue: ResolvedQueue, seconds: number): number {
  const p = queue.rttPreference;
  if (seconds >= p.anyRegionAfterSeconds) return Infinity;
  return p.initialMs + p.perSecondMs * seconds;
}

/**
 * Whether a group of humans may play together as far as relays go: its best
 * region must be within the widest tolerance of its members (the preference
 * relaxes to any region with waiting) and, only when an operator opted in to
 * `maxRttMs`, within that cap.
 */
export function regionAcceptable(
  queue: ResolvedQueue,
  group: readonly WaitingTicket[],
  now: Date,
): boolean {
  if (group.length < 2) return true;
  const { worst } = chooseRegion(group);
  if (queue.maxRttMs !== undefined && worst > queue.maxRttMs) return false;
  const tolerance = Math.max(...group.map((t) => rttTolerance(queue, waitedSeconds(t, now))));
  return worst <= tolerance;
}

function skillOf(member: GroupMember): number {
  return matchSkill(member.kind === 'human' ? member.ticket : member.candidate);
}

/**
 * Splits members into two equal sides with the smallest difference in summed
 * skill; ties prefer spreading humans across sides, then input order.
 */
export function balanceSides(members: readonly GroupMember[]): GroupMember[][] {
  const n = members.length;
  if (n % 2 !== 0) throw new Error('an even number of members is needed');
  const half = n / 2;
  let best: { mask: number; diff: number; humanSpread: number } | undefined;
  // Member 0 is always on side 0, so each split is visited once.
  for (let mask = 0; mask < 1 << n; mask++) {
    if ((mask & 1) === 0) continue;
    let count = 0;
    let sum0 = 0;
    let sum1 = 0;
    let humans0 = 0;
    let humans1 = 0;
    for (const [i, member] of members.entries()) {
      const inZero = (mask >> i) & 1;
      if (inZero) {
        count++;
        sum0 += skillOf(member);
        if (member.kind === 'human') humans0++;
      } else {
        sum1 += skillOf(member);
        if (member.kind === 'human') humans1++;
      }
    }
    if (count !== half) continue;
    const diff = Math.abs(sum0 - sum1);
    const humanSpread = Math.abs(humans0 - humans1);
    if (
      !best ||
      diff < best.diff - 1e-9 ||
      (Math.abs(diff - best.diff) <= 1e-9 && humanSpread < best.humanSpread)
    ) {
      best = { mask, diff, humanSpread };
    }
  }
  const mask = best?.mask ?? 1;
  return [members.filter((_, i) => (mask >> i) & 1), members.filter((_, i) => !((mask >> i) & 1))];
}

/** The `count` distinct AIs whose skill is closest to `target` (ties by AI id). */
export function closestAis(
  candidates: readonly AiCandidate[],
  target: number,
  count: number,
): AiCandidate[] {
  return [...candidates]
    .sort(
      (a, b) =>
        Math.abs(matchSkill(a) - target) - Math.abs(matchSkill(b) - target) ||
        a.ai.localeCompare(b.ai),
    )
    .slice(0, count);
}

function compatible(queue: ResolvedQueue, a: WaitingTicket, b: WaitingTicket, now: Date): boolean {
  const window = Math.max(
    ratingWindow(queue, waitedSeconds(a, now)),
    ratingWindow(queue, waitedSeconds(b, now)),
  );
  return Math.abs(matchSkill(a) - matchSkill(b)) <= window;
}

function byQueuePosition(a: WaitingTicket, b: WaitingTicket): number {
  return a.createdAt.getTime() - b.createdAt.getTime() || a.id.localeCompare(b.id);
}

/**
 * Forms groups from one queue's waiting tickets of one sim version, oldest
 * ticket first. Each anchor takes the closest-rated compatible tickets (within
 * the wider of the two rating windows, pairwise) whose best shared relay is
 * within the group's widening RTT tolerance; after `anyRegionAfterSeconds`
 * any relay will do, so round trips never make a player unmatchable. The
 * group plays on the relay region minimising its worst round trip. An anchor that cannot fill its group and whose backfill delay has
 * passed gets AI seats: its compatible AI-allowing partners join it and the
 * rest are the distinct AIs closest to the humans' mean skill. AI-backfilled
 * groups need `aiCandidates`.
 */
export function planGroups(
  queue: ResolvedQueue,
  tickets: readonly WaitingTicket[],
  aiCandidates: readonly AiCandidate[],
  now: Date,
): PlannedGroup[] {
  const size = modeSeats(queue.mode);
  const ordered = [...tickets].sort(byQueuePosition);
  const used = new Set<string>();
  const groups: PlannedGroup[] = [];

  for (const anchor of ordered) {
    if (used.has(anchor.id)) continue;
    const group: WaitingTicket[] = [anchor];
    const candidates = ordered
      .filter((t) => t !== anchor && !used.has(t.id) && compatible(queue, anchor, t, now))
      .sort(
        (a, b) =>
          Math.abs(matchSkill(a) - matchSkill(anchor)) -
            Math.abs(matchSkill(b) - matchSkill(anchor)) || byQueuePosition(a, b),
      );
    for (const candidate of candidates) {
      if (group.length === size) break;
      if (!group.every((member) => compatible(queue, member, candidate, now))) continue;
      if (!regionAcceptable(queue, [...group, candidate], now)) continue;
      group.push(candidate);
    }

    let humans = group;
    let backfilled = false;
    if (group.length < size) {
      if (!backfillDue(queue, anchor, now) || aiCandidates.length === 0) continue;
      humans = group.filter((t) => t.allowAi);
      backfilled = true;
    }
    const region = chooseRegion(humans);
    const members: GroupMember[] = humans.map((ticket) => ({ kind: 'human', ticket }));
    if (backfilled) {
      const target = humans.reduce((sum, t) => sum + matchSkill(t), 0) / humans.length;
      const ais = closestAis(aiCandidates, target, size - humans.length);
      if (ais.length < size - humans.length) continue;
      members.push(...ais.map((candidate): GroupMember => ({ kind: 'ai', candidate })));
    }
    const sides =
      QUEUE_MODES[queue.mode].seatsPerSide === 1 ? members.map((m) => [m]) : balanceSides(members);
    for (const t of humans) used.add(t.id);
    groups.push({ sides, region: region.region, backfilled });
  }
  return groups;
}
