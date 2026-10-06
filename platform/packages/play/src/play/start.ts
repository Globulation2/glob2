import { catalogRulesVersion, checkBuildingCatalogHash } from '@glob2/protocol/node';
// The match start sequence shared by rooms (API) and quick-match queues
// (worker): access checks, map, `matches` row with the setup and a seed the
// platform chose, and relay placement. Tickets are signed by the API replica
// that delivers match.start to each player's socket (it holds the signing
// keys), so this module never handles keys.
import { createHash } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { AccessDecision, AccessPolicy, AccessSubject, JobQueue, Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  MAX_PLAYER_NAME_BYTES,
  QUEUE_PAUSE_LIMIT,
  STANDARD_RULES,
  matchSetupProblems,
  parseSimVersionKey,
  simVersionKey,
  playerSeats,
  type AiId,
  type BuildingCatalog,
  type GeneratorDescriptor,
  type MatchSetup,
} from '@glob2/protocol';
import type { RegionRtt } from '../matchmaking/grouping.ts';
import type { MatchProposal, MatchStarter, StartedMatch } from '../matchmaking/starter.ts';
import { readRegionRtts } from '../stored.ts';
import { publishPlay } from './notify.ts';
import {
  noWarmMaps,
  requestGeneratedMap,
  waitForGeneratedMap,
  type WarmMapSource,
} from './maps.ts';
import { placeMatch, type PlacementRequest, type RelayCandidate } from './relays.ts';

type Db = Kysely<Database>;

export type StartErrorCode = 'unavailable' | 'access_denied' | 'conflict';

/** A match that cannot start, with a protocol error code for the caller. */
export class StartError extends Error {
  readonly code: StartErrorCode;
  readonly details: unknown;
  constructor(code: StartErrorCode, message: string, details?: unknown) {
    super(message);
    this.name = 'StartError';
    this.code = code;
    this.details = details;
  }
}

/** The AccessPolicy subject for an account: kind, role and active entitlements. */
export async function accessSubject(db: Db, accountId: string): Promise<AccessSubject | undefined> {
  const account = await db
    .selectFrom('accounts')
    .select(['id', 'kind', 'role', 'status'])
    .where('id', '=', accountId)
    .executeTakeFirst();
  if (!account || account.status !== 'active') return undefined;
  return {
    accountId: account.id,
    kind: account.kind,
    role: account.role,
    entitlements: await activeEntitlements(db, accountId),
  };
}

export async function activeEntitlements(db: Db, accountId: string): Promise<string[]> {
  const rows = await db
    .selectFrom('entitlements')
    .select('entitlement')
    .distinct()
    .where('account_id', '=', accountId)
    .where('revoked_at', 'is', null)
    .where((eb) => eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]))
    .orderBy('entitlement')
    .execute();
  return rows.map((r) => r.entitlement);
}

/** Throws access_denied unless the decision allows. */
export function requireAllowed(decision: AccessDecision): void {
  if (!decision.allowed) {
    throw new StartError('access_denied', decision.reason, {
      ...(decision.requiredEntitlement
        ? { requiredEntitlement: decision.requiredEntitlement }
        : {}),
    });
  }
}

/** The value, or an error naming what was missing (for invariants the types cannot express). */
export function must<T>(value: T | null | undefined, what: string): T {
  if (value === undefined || value === null) throw new Error(`missing ${what}`);
  return value;
}

/** Parses a stored sim version key (stored keys are always valid). */
export function storedSimVersion(key: string) {
  return must(parseSimVersionKey(key), `sim version ${key}`);
}

/** A uint32 derived from a key, so a retried start picks the same seeds. */
export function derivedSeed(key: string): number {
  return createHash('sha256').update(key).digest().readUInt32BE(0);
}

/** Cuts a name to at most `bytes` UTF-8 bytes without splitting a character. */
export function truncateUtf8(value: string, bytes = MAX_PLAYER_NAME_BYTES): string {
  let out = '';
  let used = 0;
  for (const char of value) {
    const size = Buffer.byteLength(char);
    if (used + size > bytes) break;
    out += char;
    used += size;
  }
  return out || '?';
}

/** Display name of an AI seat, e.g. "Cortex". */
export function aiDisplayName(ai: AiId): string {
  return ai === 'none' ? 'Nobody' : ai.charAt(0).toUpperCase() + ai.slice(1);
}

// ------------------------------------------------------------ match creation

export interface CreateMatchRequest {
  setup: MatchSetup;
  origin: 'room' | 'queue';
  roomId?: string;
  queueId?: string;
  proposalId?: string;
  rated: boolean;
  /** Rating entity of each seat, by seat number (queue matches). */
  ratingEntities?: ReadonlyMap<number, string | null>;
  placement: PlacementRequest;
}

export interface CreatedMatch {
  matchId: string;
  relay: RelayCandidate;
  /** False when a match for the proposal already existed. */
  created: boolean;
}

function isUniqueViolation(error: unknown): boolean {
  return (error as { code?: string } | null)?.code === '23505';
}

/**
 * Places a match on a relay and records it ('starting'). Fails with
 * `unavailable` when no relay can take it. Idempotent per proposal id.
 */
export async function createMatch(db: Db, request: CreateMatchRequest): Promise<CreatedMatch> {
  const setup = { ...request.setup };
  let mapCatalog: BuildingCatalog | undefined;
  for (const table of ['map_versions', 'map_uploads', 'generated_maps'] as const) {
    const row =
      table === 'map_versions'
        ? await db
            .selectFrom(table)
            .select('building_catalog')
            .where('hash', '=', setup.map.hash)
            .executeTakeFirst()
        : table === 'map_uploads'
          ? await db
              .selectFrom(table)
              .select('building_catalog')
              .where('blob_sha256', '=', setup.map.hash)
              .where('sim_version', '=', simVersionKey(setup.simVersion))
              .executeTakeFirst()
          : await db
              .selectFrom(table)
              .select('building_catalog')
              .where('map_hash', '=', setup.map.hash)
              .where('sim_version', '=', simVersionKey(setup.simVersion))
              .executeTakeFirst();
    if (row?.building_catalog) {
      mapCatalog = row.building_catalog as BuildingCatalog;
      break;
    }
  }
  if (mapCatalog) {
    if (setup.buildingCatalog && setup.buildingCatalog.hash !== mapCatalog.hash)
      throw new Error('match building catalog differs from its validated map');
    setup.buildingCatalog = mapCatalog;
  }
  if (setup.buildingCatalog) checkBuildingCatalogHash(setup.buildingCatalog);
  const problems = matchSetupProblems(setup);
  if (problems.length > 0) {
    throw new Error(`invalid setup: ${problems.map((p) => `${p.path} ${p.message}`).join('; ')}`);
  }
  if (request.proposalId) {
    const existing = await existingProposalMatch(db, request.proposalId);
    if (existing) return existing;
  }
  const relay = await placeMatch(db, request.placement);
  if (!relay) throw new StartError('unavailable', 'No relay is available to host the match.');
  try {
    const matchId = await db.transaction().execute(async (trx) => {
      const match = await trx
        .insertInto('matches')
        .values({
          sim_version: `${setup.simVersion.versionMinor}-${setup.simVersion.netProtocol}-${setup.simVersion.dataHash}`,
          rules_identity: simVersionKey(
            catalogRulesVersion(setup.simVersion, setup.buildingCatalog?.hash),
          ),
          origin: request.origin,
          room_id: request.roomId ?? null,
          queue_id: request.origin === 'queue' ? (request.queueId ?? null) : null,
          rated: request.rated,
          status: 'starting',
          setup: JSON.stringify(setup),
          seed: setup.seed,
          map_hash: setup.map.hash,
          relay_id: relay.id,
          relay_assigned_at: sql<Date>`now()`,
          relay_attempts: 1,
          proposal_id: request.proposalId ?? null,
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      await trx
        .insertInto('match_participants')
        .values(
          playerSeats(setup).map((seat) => ({
            match_id: match.id,
            seat: seat.seat,
            team: seat.team,
            kind: seat.kind,
            account_id: seat.kind === 'human' ? (seat.accountId ?? null) : null,
            ai_id: seat.kind === 'ai' ? seat.ai : null,
            rating_entity_id: request.ratingEntities?.get(seat.seat) ?? null,
            display_name: seat.name,
          })),
        )
        .execute();
      return match.id;
    });
    return { matchId, relay, created: true };
  } catch (error) {
    if (request.proposalId && isUniqueViolation(error)) {
      const existing = await existingProposalMatch(db, request.proposalId);
      if (existing) return existing;
    }
    throw error;
  }
}

async function existingProposalMatch(
  db: Db,
  proposalId: string,
): Promise<CreatedMatch | undefined> {
  const row = await db
    .selectFrom('matches as m')
    .leftJoin('relays as r', 'r.id', 'm.relay_id')
    .select(['m.id', 'r.id as relay_id', 'r.public_url', 'r.region', 'r.max_matches'])
    .where('m.proposal_id', '=', proposalId)
    .executeTakeFirst();
  if (!row) return undefined;
  return {
    matchId: row.id,
    relay: {
      id: row.relay_id ?? '',
      publicUrl: row.public_url ?? '',
      region: row.region ?? '',
      maxMatches: row.max_matches ?? 1,
      load: 0,
    },
    created: false,
  };
}

/** Relay round trips of a match's human players (room members or queue tickets). */
export async function matchPlacementProbes(db: Db, matchId: string): Promise<RegionRtt[][]> {
  const match = await db
    .selectFrom('matches')
    .select(['origin', 'room_id'])
    .where('id', '=', matchId)
    .executeTakeFirst();
  if (!match) return [];
  if (match.origin === 'room' && match.room_id) {
    const rows = await db
      .selectFrom('match_participants as p')
      .innerJoin('room_members as rm', (join) =>
        join
          .onRef('rm.account_id', '=', 'p.account_id')
          .on('rm.room_id', '=', must(match.room_id, 'room id')),
      )
      .select('rm.region_rtts')
      .where('p.match_id', '=', matchId)
      .execute();
    return rows.map((r) => readRegionRtts(r.region_rtts));
  }
  const rows = await db
    .selectFrom('queue_tickets')
    .select('region_rtts')
    .where('match_id', '=', matchId)
    .execute();
  return rows.map((r) => readRegionRtts(r.region_rtts));
}

/** How many times a starting match may be moved to another relay. */
export const MAX_RELAY_ATTEMPTS = 5;

export type MoveOutcome =
  | { moved: true; relay: RelayCandidate }
  | { moved: false; reason: 'not_starting' | 'no_relay' | 'too_many_attempts' | 'not_found' };

/**
 * Moves a match nobody has reached yet to another relay, after its relay
 * refused it as new (draining or full). Publishes match.start to every player.
 */
export async function moveMatchToAnotherRelay(db: Db, matchId: string): Promise<MoveOutcome> {
  const probes = await matchPlacementProbes(db, matchId);
  return db.transaction().execute(async (trx) => {
    const match = await trx
      .selectFrom('matches')
      .select(['status', 'relay_id', 'relay_attempts'])
      .where('id', '=', matchId)
      .forUpdate()
      .executeTakeFirst();
    if (!match) return { moved: false, reason: 'not_found' };
    if (match.status !== 'starting') return { moved: false, reason: 'not_starting' };
    if (match.relay_attempts >= MAX_RELAY_ATTEMPTS) {
      return { moved: false, reason: 'too_many_attempts' };
    }
    const relay = await placeMatch(trx, {
      players: probes,
      exclude: match.relay_id ? [match.relay_id] : [],
    });
    if (!relay) return { moved: false, reason: 'no_relay' };
    await trx
      .updateTable('matches')
      .set({
        relay_id: relay.id,
        relay_assigned_at: sql<Date>`now()`,
        relay_attempts: match.relay_attempts + 1,
      })
      .where('id', '=', matchId)
      .execute();
    await publishPlay(trx, { t: 'matchStart', matchId });
    return { moved: true, relay };
  });
}

// --------------------------------------------------------- queue match starter

export interface PlatformMatchStarterOptions {
  db: Db;
  jobs: JobQueue;
  access: AccessPolicy;
  warmMaps?: WarmMapSource;
  /** How long to wait for an on-demand generate-map job (default 60 s). */
  generationTimeoutMs?: number;
  logger?: Logger;
}

/**
 * The MatchStarter for quick-match proposals. Idempotent per proposal id:
 * seeds derive from the proposal id, generation is shared by descriptor, and
 * an existing match for the proposal is returned as is. Players receive
 * match.start from the API when the matchmaker's queue.matchFound reaches
 * their socket (it follows a successful start), so this starter sends nothing.
 */
export class PlatformMatchStarter implements MatchStarter {
  private readonly options: PlatformMatchStarterOptions;

  constructor(options: PlatformMatchStarterOptions) {
    this.options = options;
  }

  async start(proposal: MatchProposal): Promise<StartedMatch> {
    const { db } = this.options;
    const existing = await existingProposalMatch(db, proposal.id);
    if (existing) return { matchId: existing.matchId };
    const simVersion = parseSimVersionKey(proposal.simVersion);
    if (!simVersion) throw new Error(`bad sim version ${proposal.simVersion}`);

    const humans = proposal.seats.flatMap((s) =>
      s.kind === 'human' && s.accountId ? [{ ...s, accountId: s.accountId }] : [],
    );
    for (const seat of humans) {
      const subject = await accessSubject(db, seat.accountId);
      if (!subject) throw new StartError('access_denied', 'A player account is not active.');
      requireAllowed(
        await this.options.access.canQueue(subject, {
          queueId: proposal.queueId,
          rated: proposal.rated,
          simVersion,
        }),
      );
    }

    const { generator, mapHash } = await this.map(proposal);
    const names = new Map(
      humans.length === 0
        ? []
        : (
            await db
              .selectFrom('accounts')
              .select(['id', 'display_name'])
              .where(
                'id',
                'in',
                humans.map((s) => s.accountId),
              )
              .execute()
          ).map((a) => [a.id, a.display_name]),
    );
    const setup = queueMatchSetup(proposal, {
      seed: derivedSeed(`${proposal.id}:game`),
      generator,
      mapHash,
      names,
    });
    const tickets = humans.flatMap((s) => (s.ticketId ? [s.ticketId] : []));
    const probes =
      tickets.length === 0
        ? []
        : (
            await db
              .selectFrom('queue_tickets')
              .select('region_rtts')
              .where('id', 'in', tickets)
              .execute()
          ).map((r) => readRegionRtts(r.region_rtts));
    const created = await createMatch(db, {
      setup,
      origin: 'queue',
      queueId: proposal.queueId,
      proposalId: proposal.id,
      rated: proposal.rated,
      ratingEntities: new Map(proposal.seats.map((s) => [s.slot, s.ratingEntityId])),
      placement: { players: probes, preferredRegion: proposal.region },
    });
    this.options.logger?.info(
      { proposal: proposal.id, match: created.matchId, relay: created.relay.id },
      'queue match placed',
    );
    return { matchId: created.matchId };
  }

  private async map(
    proposal: MatchProposal,
  ): Promise<{ generator: GeneratorDescriptor; mapHash: string }> {
    const warm = await (this.options.warmMaps ?? noWarmMaps).takeWarmMap(
      proposal.queueId,
      proposal.simVersion,
      { entry: proposal.map },
    );
    if (warm) return { generator: warm.generator, mapHash: warm.mapHash };
    const generator: GeneratorDescriptor = {
      ...proposal.map,
      params: { ...proposal.map.params, teams: proposal.seats.length },
      seed: derivedSeed(`${proposal.id}:map`),
    };
    let state = await requestGeneratedMap(
      this.options.db,
      this.options.jobs,
      generator,
      proposal.simVersion,
    );
    if (state.status === 'pending') {
      state = await waitForGeneratedMap(this.options.db, generator, proposal.simVersion, {
        timeoutMs: this.options.generationTimeoutMs ?? 60_000,
      });
    }
    if (state.status === 'ready') return { generator, mapHash: state.mapHash };
    throw new StartError(
      'unavailable',
      state.status === 'failed'
        ? `map generation failed: ${state.failure}`
        : 'map generation timed out',
    );
  }
}

/** The MatchSetup of a queue match: seat = slot = map team, alliance = side. */
export function queueMatchSetup(
  proposal: MatchProposal,
  options: {
    seed: number;
    generator: GeneratorDescriptor;
    mapHash: string;
    names?: ReadonlyMap<string, string>;
  },
): MatchSetup {
  const simVersion = parseSimVersionKey(proposal.simVersion);
  if (!simVersion) throw new Error(`bad sim version ${proposal.simVersion}`);
  const seats = [...proposal.seats].sort((a, b) => a.slot - b.slot);
  return {
    schemaVersion: 1,
    simVersion,
    seed: options.seed,
    map: { kind: 'generated', generator: options.generator, hash: options.mapHash },
    teams: seats.map((s) => ({ team: s.slot, alliance: s.side })),
    seats: seats.map((s) =>
      s.kind === 'ai' && s.ai
        ? { seat: s.slot, kind: 'ai', team: s.slot, name: aiDisplayName(s.ai), ai: s.ai }
        : {
            seat: s.slot,
            kind: 'human',
            team: s.slot,
            name: truncateUtf8(
              (s.accountId && options.names?.get(s.accountId)) || `Player ${s.slot + 1}`,
            ),
            ...(s.accountId ? { accountId: s.accountId } : {}),
          },
    ),
    rules: STANDARD_RULES,
    experiments: [],
    // Quick and rated matches limit pausing; rooms do not.
    pauseLimit: QUEUE_PAUSE_LIMIT,
  };
}
