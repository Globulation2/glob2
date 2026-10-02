// Relays: registration, heartbeats and match placement.
//
// A relay registers at start-up and heartbeats every RELAY_HEARTBEAT_SECONDS
// with its load and drain state. A relay is available when it is not draining,
// was heard from within RELAY_STALE_SECONDS and has room for another match.
//
// Placement never fails for distance: there may be only a handful of relays,
// so the platform takes the available relay that minimises the worst round
// trip of the players who measured its region, and otherwise the least loaded
// one (preferring a requested region). It fails only when no relay is
// available at all.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { RelayHeartbeat, RelayRegistration } from '@glob2/protocol';
import type { RegionRtt } from '../matchmaking/grouping.ts';

type Db = Kysely<Database>;

/** Heartbeat interval relays are told to use. */
export const RELAY_HEARTBEAT_SECONDS = 15;
/** A relay not heard from for this long (three missed heartbeats) gets no matches. */
export const RELAY_STALE_SECONDS = 45;

export interface RelayCandidate {
  id: string;
  publicUrl: string;
  region: string;
  maxMatches: number;
  /** max(matches the relay reports, matches the platform placed on it and has not seen end). */
  load: number;
}

export interface PlacementRequest {
  /** Round-trip probes of each human player (empty when a player sent none). */
  players: readonly (readonly RegionRtt[])[];
  /** Region to prefer when nobody measured any relay's region (a queue proposal's region). */
  preferredRegion?: string | null;
  /** Relays not to use (one that refused the match). */
  exclude?: readonly string[];
}

/**
 * Picks a relay among available candidates (pure). With probes, the relay
 * whose region has the smallest worst round trip among the players who
 * measured it wins; relays in regions nobody measured come after every
 * measured one. Ties, and the no-probe case, go to the preferred region, then
 * the lowest load fraction, then the relay id.
 */
export function chooseRelay(
  candidates: readonly RelayCandidate[],
  request: PlacementRequest,
): RelayCandidate | undefined {
  const exclude = new Set(request.exclude ?? []);
  const scored = candidates
    .filter((c) => !exclude.has(c.id) && c.load < c.maxMatches)
    .map((candidate) => {
      let worst: number | undefined;
      for (const probes of request.players) {
        const probe = probes.find((p) => p.region === candidate.region);
        if (probe) worst = Math.max(worst ?? 0, probe.rttMs);
      }
      return {
        candidate,
        worst: worst ?? Number.POSITIVE_INFINITY,
        preferred: request.preferredRegion === candidate.region ? 0 : 1,
        fraction: candidate.load / candidate.maxMatches,
      };
    });
  scored.sort(
    (a, b) =>
      a.worst - b.worst ||
      a.preferred - b.preferred ||
      a.fraction - b.fraction ||
      (a.candidate.id < b.candidate.id ? -1 : a.candidate.id > b.candidate.id ? 1 : 0),
  );
  return scored[0]?.candidate;
}

/** Relays that may take a new match now, with their load. */
export async function availableRelays(db: Db): Promise<RelayCandidate[]> {
  const rows = await db
    .selectFrom('relays as r')
    .select((eb) => [
      'r.id',
      'r.public_url',
      'r.region',
      'r.max_matches',
      'r.active_matches',
      eb
        .selectFrom('matches as m')
        .select(sql<number>`count(*)::int`.as('n'))
        .whereRef('m.relay_id', '=', 'r.id')
        .where('m.status', 'in', ['starting', 'running'])
        .as('assigned'),
    ])
    .where('r.draining', '=', false)
    .where(
      'r.last_heartbeat_at',
      '>',
      sql<Date>`now() - make_interval(secs => ${RELAY_STALE_SECONDS})`,
    )
    .execute();
  return rows.map((row) => ({
    id: row.id,
    publicUrl: row.public_url,
    region: row.region,
    maxMatches: row.max_matches,
    load: Math.max(row.active_matches, row.assigned ?? 0),
  }));
}

/** Chooses a relay for a new match, or undefined when none is available. */
export async function placeMatch(
  db: Db,
  request: PlacementRequest,
): Promise<RelayCandidate | undefined> {
  return chooseRelay(await availableRelays(db), request);
}

/** Records a relay's registration (start-up, or after the platform forgot it). */
export async function registerRelay(db: Db, registration: RelayRegistration): Promise<void> {
  const values = {
    public_url: registration.publicUrl,
    region: registration.region,
    build: registration.build,
    turn_protocol: registration.turnProtocol,
    max_matches: registration.capacity.maxMatches,
    active_matches: registration.load.matches,
    connections: registration.load.connections,
    cpu: registration.load.cpu ?? null,
    draining: registration.draining,
    registered_at: sql<Date>`now()`,
    last_heartbeat_at: sql<Date>`now()`,
  };
  await db
    .insertInto('relays')
    .values({ id: registration.relayId, ...values })
    .onConflict((oc) => oc.column('id').doUpdateSet(values))
    .execute();
}

export type HeartbeatOutcome = { known: false } | { known: true; nowRunning: string[] };

/**
 * Applies a heartbeat. Matches the relay reports as active move from
 * 'starting' to 'running' (their players reached the relay). Unknown relays
 * must register again.
 */
export async function relayHeartbeat(db: Db, heartbeat: RelayHeartbeat): Promise<HeartbeatOutcome> {
  return db.transaction().execute(async (trx) => {
    const updated = await trx
      .updateTable('relays')
      .set({
        active_matches: heartbeat.load.matches,
        connections: heartbeat.load.connections,
        cpu: heartbeat.load.cpu ?? null,
        draining: heartbeat.draining,
        last_heartbeat_at: sql<Date>`now()`,
      })
      .where('id', '=', heartbeat.relayId)
      .executeTakeFirst();
    if (updated.numUpdatedRows === 0n) return { known: false };
    let nowRunning: string[] = [];
    if (heartbeat.activeMatchIds.length > 0) {
      const rows = await trx
        .updateTable('matches')
        .set({ status: 'running', started_at: sql<Date>`coalesce(started_at, now())` })
        .where('id', 'in', heartbeat.activeMatchIds)
        .where('status', '=', 'starting')
        .where('relay_id', '=', heartbeat.relayId)
        .returning('id')
        .execute();
      nowRunning = rows.map((r) => r.id);
    }
    return { known: true, nowRunning };
  });
}
