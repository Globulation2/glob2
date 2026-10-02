// Rooms: a host, members, seats (human, AI, open or locked), map, teams and
// alliances, rules, chat, and the start of a match. State lives in Postgres;
// every change bumps the room revision and publishes `{t: "room"}` so each
// API replica sends room.state to the members whose sockets it holds.
//
// Seat i always plays map team i; `teams[i].alliance` groups teams into
// sides. The UX rules below are provisional defaults from the mock-ups and
// are collected in ROOM_RULES so they are easy to change.
import { randomInt } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { AccessPolicy, JobQueue, Logger } from '@glob2/core';
import type { Account, Database } from '@glob2/db';
import {
  STANDARD_RULES,
  parseSimVersionKey,
  simVersionKey,
  type AiId,
  type MatchRules,
  type MatchSetup,
  type RoomChatMessage,
  type RoomMapSelection,
  type RoomSeat,
  type RoomState,
  type RoomSummary,
  type Seat,
  type SetupTeam,
  type SimVersion,
} from '@glob2/protocol';
import {
  StartError,
  accessSubject,
  aiDisplayName,
  createMatch,
  must,
  publishPlay,
  requestGeneratedMap,
  storedSimVersion,
  truncateUtf8,
  type RegionRtt,
} from '@glob2/worker';
import { apiError } from '../errors.ts';

type Db = Kysely<Database>;

/** Provisional room UX defaults (mock-ups); change here. */
export const ROOM_RULES = {
  /** Non-hosts may move themselves into open seats. Teams, AIs and locks stay host-only. */
  membersChooseSeats: true,
  /** Changing the map, teams, rules or experiments clears every Ready. */
  settingsClearReady: true,
  /** The host leaving closes the room (no host migration). */
  hostLeaveCloses: true,
  /** The host is ready by starting; there is no force start past unready players. */
  hostImplicitlyReady: true,
  /** Seats (human or AI) that must be occupied to start. */
  minOccupiedSeats: 2,
  /** Teams of a room without a map. */
  defaultTeams: 2,
  maxMembers: 24,
  /** An open room closes when its host has been disconnected this long. */
  hostGraceSeconds: 120,
  /** Disconnected non-host members are removed from open rooms after this long. */
  memberGraceSeconds: 600,
  /** Open rooms without any change for this long close. */
  idleSeconds: 12 * 3600,
  /** Invite code length; 32 symbols each, 50 bits for 10. */
  codeLength: 10,
  /** A kicked player cannot rejoin the room for this long. */
  kickBanSeconds: 600,
} as const;

/** Unambiguous code alphabet (no 0/O, 1/I). */
const CODE_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

export function newInviteCode(length: number = ROOM_RULES.codeLength): string {
  let code = '';
  for (let i = 0; i < length; i++) code += CODE_ALPHABET[randomInt(CODE_ALPHABET.length)];
  return code;
}

/** Codes are shown upper-case; lookups accept either case. */
export function normalizeCode(code: string): string {
  return code.toUpperCase();
}

/** Room settings as stored in rooms.settings. */
export interface RoomSettings {
  map?: RoomMapSelection;
  mapStatus?: 'ready' | 'pending' | 'failed';
  mapProblem?: string;
  /** Engine job the map waits for (generated map or upload validation). */
  mapJobId?: string;
  teams: SetupTeam[];
  rules: MatchRules;
  experiments: string[];
}

export interface MapResolution {
  selection: RoomMapSelection;
  status: 'ready' | 'pending' | 'failed';
  problem?: string;
  jobId?: string;
  /** Map team count when known. */
  teamCount?: number;
}

export interface RoomServiceOptions {
  db: Db;
  jobs: JobQueue;
  access: AccessPolicy;
  origin: string;
  logger: Logger;
}

type SeatRow = {
  seat: number;
  team: number;
  occupant: 'open' | 'human' | 'ai';
  account_id: string | null;
  ai_id: string | null;
  ai_name: string | null;
  ready: boolean;
  locked: boolean;
};

export class RoomService {
  private readonly db: Db;
  private readonly jobs: JobQueue;
  private readonly access: AccessPolicy;
  private readonly origin: string;
  private readonly logger: Logger;

  constructor(options: RoomServiceOptions) {
    this.db = options.db;
    this.jobs = options.jobs;
    this.access = options.access;
    this.origin = options.origin;
    this.logger = options.logger;
  }

  get database(): Db {
    return this.db;
  }

  inviteUrl(code: string): string {
    return `${this.origin}/j/${code}`;
  }

  // ------------------------------------------------------------------ state

  async state(roomId: string, db: Db = this.db): Promise<RoomState | undefined> {
    const room = await db
      .selectFrom('rooms')
      .selectAll()
      .where('id', '=', roomId)
      .executeTakeFirst();
    if (!room) return undefined;
    const settings = room.settings as unknown as RoomSettings;
    const seats = await db
      .selectFrom('room_seats as s')
      .leftJoin('accounts as a', 'a.id', 's.account_id')
      .select([
        's.seat',
        's.team',
        's.occupant',
        's.account_id',
        's.ai_id',
        's.ai_name',
        's.ready',
        's.locked',
        'a.display_name',
      ])
      .where('s.room_id', '=', roomId)
      .orderBy('s.seat')
      .execute();
    const members = await db
      .selectFrom('room_members as m')
      .innerJoin('accounts as a', 'a.id', 'm.account_id')
      .select(['m.account_id', 'm.connected', 'a.display_name', 'a.kind'])
      .where('m.room_id', '=', roomId)
      .orderBy('m.joined_at')
      .execute();
    const seatOf = new Map(seats.flatMap((s) => (s.account_id ? [[s.account_id, s.seat]] : [])));
    const simVersion = parseSimVersionKey(room.sim_version);
    if (!simVersion) throw new Error(`room ${roomId} has a bad sim version`);
    return {
      id: room.id,
      code: room.code,
      inviteUrl: this.inviteUrl(room.code),
      name: room.name,
      visibility: room.visibility,
      status: room.status,
      hostAccountId: room.host_account_id,
      simVersion,
      ...(settings.map ? { map: settings.map } : {}),
      ...(settings.mapStatus ? { mapStatus: settings.mapStatus } : {}),
      ...(settings.mapProblem ? { mapProblem: settings.mapProblem } : {}),
      teams: settings.teams,
      seats: seats.map((s): RoomSeat => ({
        seat: s.seat,
        team: s.team,
        occupant:
          s.occupant === 'human' && s.account_id
            ? {
                kind: 'human',
                accountId: s.account_id,
                displayName: s.display_name ?? '?',
                ready: s.ready,
              }
            : s.occupant === 'ai' && s.ai_id
              ? {
                  kind: 'ai',
                  ai: s.ai_id as AiId,
                  name: s.ai_name ?? aiDisplayName(s.ai_id as AiId),
                }
              : { kind: 'open' },
        ...(s.locked ? { locked: true } : {}),
      })),
      rules: settings.rules,
      experiments: settings.experiments,
      members: members.map((m) => ({
        accountId: m.account_id,
        displayName: m.display_name,
        kind: m.kind,
        connected: m.connected,
        ...(seatOf.has(m.account_id) ? { seat: must(seatOf.get(m.account_id), 'seat') } : {}),
      })),
      ...(room.match_id ? { matchId: room.match_id } : {}),
      revision: room.revision,
      createdAt: room.created_at.toISOString(),
    };
  }

  private async mustState(roomId: string): Promise<RoomState> {
    return must(await this.state(roomId), `room ${roomId}`);
  }

  async memberIds(roomId: string, db: Db = this.db): Promise<string[]> {
    const rows = await db
      .selectFrom('room_members')
      .select('account_id')
      .where('room_id', '=', roomId)
      .execute();
    return rows.map((r) => r.account_id);
  }

  async chatMessage(messageId: string): Promise<RoomChatMessage | undefined> {
    const row = await this.db
      .selectFrom('room_chat_messages as c')
      .innerJoin('accounts as a', 'a.id', 'c.account_id')
      .select(['c.id', 'c.room_id', 'c.account_id', 'c.text', 'c.sent_at', 'a.display_name'])
      .where('c.id', '=', messageId)
      .executeTakeFirst();
    if (!row) return undefined;
    return {
      id: row.id,
      roomId: row.room_id,
      accountId: row.account_id,
      displayName: row.display_name,
      text: row.text,
      sentAt: row.sent_at.toISOString(),
    };
  }

  /** Public open rooms of a sim version, most recently changed first. */
  async listPublic(
    simVersion: string,
    limit: number,
    cursor?: { updatedAt: Date; id: string },
  ): Promise<{ items: RoomSummary[]; next?: { updatedAt: Date; id: string } }> {
    let query = this.db
      .selectFrom('rooms as r')
      .innerJoin('accounts as h', 'h.id', 'r.host_account_id')
      .select((eb) => [
        'r.id',
        'r.code',
        'r.name',
        'r.sim_version',
        'r.status',
        'r.updated_at',
        'r.settings',
        'h.display_name',
        eb
          .selectFrom('room_seats as s')
          .select(sql<number>`count(*)::int`.as('n'))
          .whereRef('s.room_id', '=', 'r.id')
          .where('s.locked', '=', false)
          .as('seats_total'),
        eb
          .selectFrom('room_seats as s')
          .select(sql<number>`count(*)::int`.as('n'))
          .whereRef('s.room_id', '=', 'r.id')
          .where('s.occupant', '!=', 'open')
          .as('seats_taken'),
      ])
      .where('r.status', '=', 'open')
      .where('r.visibility', '=', 'public')
      .where('r.sim_version', '=', simVersion)
      .orderBy('r.updated_at', 'desc')
      .orderBy('r.id', 'desc')
      .limit(limit + 1);
    if (cursor) {
      query = query.where((eb) =>
        eb.or([
          eb('r.updated_at', '<', cursor.updatedAt),
          eb.and([eb('r.updated_at', '=', cursor.updatedAt), eb('r.id', '<', cursor.id)]),
        ]),
      );
    }
    const rows = await query.execute();
    const page = rows.slice(0, limit);
    const last = page[page.length - 1];
    return {
      items: page.map((row) => {
        const settings = row.settings as unknown as RoomSettings;
        return {
          id: row.id,
          code: row.code,
          name: row.name,
          hostDisplayName: row.display_name,
          simVersion: storedSimVersion(row.sim_version),
          status: row.status,
          seatsTotal: row.seats_total ?? 0,
          seatsTaken: row.seats_taken ?? 0,
          ...(settings.map?.kind === 'generated'
            ? { mapTitle: settings.map.generator.generatorId }
            : {}),
        };
      }),
      ...(rows.length > limit && last ? { next: { updatedAt: last.updated_at, id: last.id } } : {}),
    };
  }

  /** The room behind an invite code (any status), for landing pages and previews. */
  async byCode(code: string) {
    return this.db
      .selectFrom('rooms as r')
      .innerJoin('accounts as h', 'h.id', 'r.host_account_id')
      .select((eb) => [
        'r.id',
        'r.code',
        'r.name',
        'r.status',
        'r.sim_version',
        'h.display_name as host_display_name',
        eb
          .selectFrom('room_seats as s')
          .select(sql<number>`count(*)::int`.as('n'))
          .whereRef('s.room_id', '=', 'r.id')
          .where('s.locked', '=', false)
          .as('seats_total'),
        eb
          .selectFrom('room_seats as s')
          .select(sql<number>`count(*)::int`.as('n'))
          .whereRef('s.room_id', '=', 'r.id')
          .where('s.occupant', '!=', 'open')
          .as('seats_taken'),
      ])
      .where('r.code', '=', normalizeCode(code))
      .executeTakeFirst();
  }

  // ------------------------------------------------------------------- maps

  /** Checks a map choice and finds its team count (starting generation if needed). */
  async resolveMap(
    selection: RoomMapSelection,
    simVersion: string,
    hostId: string,
  ): Promise<MapResolution> {
    if (selection.kind === 'catalog') {
      let query = this.db
        .selectFrom('map_versions as v')
        .innerJoin('maps as m', 'm.id', 'v.map_id')
        .select(['v.team_count', 'm.id'])
        .where('v.hash', '=', selection.hash)
        .where('v.validation', '=', 'valid')
        // Files saved by a newer engine than the room's cannot load.
        .where((eb) =>
          eb.or([
            eb('v.min_version_minor', 'is', null),
            eb('v.min_version_minor', '<=', parseSimVersionKey(simVersion)?.versionMinor ?? 0),
          ]),
        )
        .where('m.hidden', '=', false)
        .where((eb) =>
          eb.or([eb('m.visibility', '!=', 'private'), eb('m.owner_account_id', '=', hostId)]),
        );
      if (selection.mapId) query = query.where('m.id', '=', selection.mapId);
      const row = await query.executeTakeFirst();
      if (!row?.team_count) throw apiError('bad_request', 'Unknown or unavailable catalog map.');
      return { selection, status: 'ready', teamCount: row.team_count };
    }
    if (selection.kind === 'upload') {
      const row = await this.db
        .selectFrom('map_uploads')
        .select(['status', 'team_count', 'failure', 'job_id'])
        .where('blob_sha256', '=', selection.hash)
        .where('format', '=', selection.format)
        .where('sim_version', '=', simVersion)
        .where('owner_account_id', '=', hostId)
        .orderBy('created_at', 'desc')
        .executeTakeFirst();
      if (!row) throw apiError('bad_request', 'Upload the map first (POST /api/v1/uploads).');
      if (row.status === 'invalid') {
        throw apiError(
          'bad_request',
          `The uploaded file is not usable: ${row.failure ?? 'invalid'}.`,
        );
      }
      const teamCount = row.team_count ?? undefined;
      if (teamCount !== undefined) {
        for (const entry of selection.reteaming ?? []) {
          if (entry.team >= teamCount) {
            throw apiError('bad_request', `Reteaming names team ${entry.team} of ${teamCount}.`);
          }
        }
      }
      return row.status === 'valid'
        ? { selection, status: 'ready', ...(teamCount ? { teamCount } : {}) }
        : { selection, status: 'pending', ...(row.job_id ? { jobId: row.job_id } : {}) };
    }
    const teams = selection.generator.params['teams'];
    if (teams === undefined || teams < 1 || teams > 12) {
      throw apiError('bad_request', 'A generated room map needs params.teams between 1 and 12.');
    }
    const withoutHash = { ...selection };
    delete withoutHash.hash;
    const state = await requestGeneratedMap(this.db, this.jobs, selection.generator, simVersion);
    if (state.status === 'ready') {
      return {
        selection: { ...withoutHash, hash: state.mapHash },
        status: 'ready',
        teamCount: teams,
      };
    }
    if (state.status === 'failed') {
      return {
        selection: withoutHash,
        status: 'failed',
        problem: state.failure,
        teamCount: teams,
      };
    }
    return {
      selection: withoutHash,
      status: 'pending',
      teamCount: teams,
      ...(state.jobId ? { jobId: state.jobId } : {}),
    };
  }

  /**
   * Re-checks the map of rooms waiting for an engine job (or one room) and
   * publishes those that changed. Safe to run on every replica: only a real
   * status change bumps the revision.
   */
  async refreshPendingMaps(filter: { roomId?: string } = {}): Promise<void> {
    // Any finished map job re-checks every pending room: one validation
    // finishes every upload of the same file, and a generation may finish
    // before its job id was stored.
    let query = this.db
      .selectFrom('rooms')
      .select(['id'])
      .where('status', '!=', 'closed')
      .where(sql<string>`settings->>'mapStatus'`, '=', 'pending');
    if (filter.roomId) query = query.where('id', '=', filter.roomId);
    const rooms = await query.execute();
    for (const { id } of rooms) {
      await this.db.transaction().execute(async (trx) => {
        const room = await this.lock(trx, id);
        const settings = room.settings as unknown as RoomSettings;
        if (room.status === 'closed' || settings.mapStatus !== 'pending' || !settings.map) return;
        let resolution: MapResolution;
        try {
          resolution = await this.resolveMap(settings.map, room.sim_version, room.host_account_id);
        } catch (error) {
          resolution = {
            selection: settings.map,
            status: 'failed',
            problem: (error as Error).message,
          };
        }
        if (resolution.status === 'pending') return;
        await this.applyMap(trx, id, settings, resolution);
        await this.bump(trx, id);
      });
    }
  }

  /** Stores a map resolution, resizing teams and seats to the map's team count. */
  private async applyMap(
    trx: Db,
    roomId: string,
    settings: RoomSettings,
    resolution: MapResolution,
  ): Promise<void> {
    const next: RoomSettings = {
      ...settings,
      map: resolution.selection,
      mapStatus: resolution.status,
    };
    delete next.mapProblem;
    delete next.mapJobId;
    if (resolution.problem) next.mapProblem = resolution.problem;
    if (resolution.jobId && resolution.status === 'pending') next.mapJobId = resolution.jobId;
    if (resolution.teamCount !== undefined) {
      next.teams = resizeTeams(settings.teams, resolution.teamCount);
      await this.resizeSeats(trx, roomId, resolution.teamCount);
    }
    await this.writeSettings(trx, roomId, next);
  }

  private async resizeSeats(trx: Db, roomId: string, count: number): Promise<void> {
    await trx
      .deleteFrom('room_seats')
      .where('room_id', '=', roomId)
      .where('seat', '>=', count)
      .execute();
    const existing = await trx
      .selectFrom('room_seats')
      .select('seat')
      .where('room_id', '=', roomId)
      .execute();
    const have = new Set(existing.map((s) => s.seat));
    const missing = [];
    for (let seat = 0; seat < count; seat++) {
      if (!have.has(seat)) missing.push({ room_id: roomId, seat, team: seat });
    }
    if (missing.length > 0) await trx.insertInto('room_seats').values(missing).execute();
  }

  // ------------------------------------------------------------- internals

  private async lock(trx: Db, roomId: string) {
    const room = await trx
      .selectFrom('rooms')
      .selectAll()
      .where('id', '=', roomId)
      .forUpdate()
      .executeTakeFirst();
    if (!room) throw apiError('not_found', 'No such room.');
    return room;
  }

  private async requireMember(trx: Db, roomId: string, accountId: string): Promise<void> {
    const member = await trx
      .selectFrom('room_members')
      .select('account_id')
      .where('room_id', '=', roomId)
      .where('account_id', '=', accountId)
      .executeTakeFirst();
    if (!member) throw apiError('not_found', 'You are not in this room.');
  }

  private async writeSettings(trx: Db, roomId: string, settings: RoomSettings): Promise<void> {
    await trx
      .updateTable('rooms')
      .set({ settings: JSON.stringify(settings) })
      .where('id', '=', roomId)
      .execute();
  }

  /** New revision; members receive room.state once the transaction commits. */
  private async bump(trx: Db, roomId: string): Promise<void> {
    await trx
      .updateTable('rooms')
      .set((eb) => ({ revision: eb('revision', '+', 1), updated_at: sql<Date>`now()` }))
      .where('id', '=', roomId)
      .execute();
    await publishPlay(trx, { t: 'room', roomId });
  }

  private async clearReady(trx: Db, roomId: string): Promise<void> {
    await trx
      .updateTable('room_seats')
      .set({ ready: false })
      .where('room_id', '=', roomId)
      .execute();
  }

  private async closeLocked(
    trx: Db,
    roomId: string,
    reason: 'host_closed' | 'kicked' | 'expired',
  ): Promise<void> {
    const accountIds = await this.memberIds(roomId, trx);
    await trx
      .updateTable('rooms')
      .set((eb) => ({
        status: 'closed',
        closed_at: sql<Date>`now()`,
        updated_at: sql<Date>`now()`,
        revision: eb('revision', '+', 1),
      }))
      .where('id', '=', roomId)
      .execute();
    await trx.deleteFrom('room_members').where('room_id', '=', roomId).execute();
    await trx
      .updateTable('room_seats')
      .set({ occupant: 'open', account_id: null, ai_id: null, ai_name: null, ready: false })
      .where('room_id', '=', roomId)
      .where('occupant', '=', 'human')
      .execute();
    await publishPlay(trx, { t: 'roomClosed', roomId, reason, accountIds });
  }

  /** Leaves (or, as host, closes) every other room the account is in. */
  private async leaveOtherRooms(accountId: string, except?: string): Promise<void> {
    const rooms = await this.db
      .selectFrom('room_members as m')
      .innerJoin('rooms as r', 'r.id', 'm.room_id')
      .select('r.id')
      .where('m.account_id', '=', accountId)
      .where('r.status', '!=', 'closed')
      .execute();
    for (const room of rooms) {
      if (room.id !== except) await this.leave(accountId, room.id);
    }
  }

  // --------------------------------------------------------------- actions

  async create(
    caller: Account,
    simVersion: SimVersion,
    params: {
      name: string;
      visibility: 'public' | 'link';
      map?: RoomMapSelection;
      rules?: MatchRules;
      experiments?: string[];
      regions?: RegionRtt[];
    },
  ): Promise<RoomState> {
    const subject = await accessSubject(this.db, caller.id);
    if (!subject) throw apiError('forbidden', 'Your account cannot host rooms.');
    const decision = await this.access.canHost(subject, {
      simVersion,
      visibility: params.visibility,
    });
    if (!decision.allowed) throw accessDenied(decision);
    const sim = simVersionKey(simVersion);
    const resolution = params.map ? await this.resolveMap(params.map, sim, caller.id) : undefined;
    await this.leaveOtherRooms(caller.id);
    const teamCount = resolution?.teamCount ?? ROOM_RULES.defaultTeams;
    const settings: RoomSettings = {
      teams: resizeTeams([], teamCount),
      rules: params.rules ?? STANDARD_RULES,
      experiments: params.experiments ?? [],
    };
    if (resolution) {
      settings.map = resolution.selection;
      settings.mapStatus = resolution.status;
      if (resolution.problem) settings.mapProblem = resolution.problem;
      if (resolution.jobId && resolution.status === 'pending') settings.mapJobId = resolution.jobId;
    }
    const roomId = await this.db.transaction().execute(async (trx) => {
      let room: { id: string } | undefined;
      for (let attempt = 0; attempt < 5 && !room; attempt++) {
        room = await trx
          .insertInto('rooms')
          .values({
            code: newInviteCode(),
            name: params.name,
            visibility: params.visibility,
            host_account_id: caller.id,
            sim_version: sim,
            settings: JSON.stringify(settings),
          })
          .onConflict((oc) => oc.column('code').doNothing())
          .returning('id')
          .executeTakeFirst();
      }
      if (!room) throw new Error('could not allocate an invite code');
      await trx
        .insertInto('room_members')
        .values({
          room_id: room.id,
          account_id: caller.id,
          region_rtts: JSON.stringify(params.regions ?? []),
        })
        .execute();
      await trx
        .insertInto('room_seats')
        .values(
          Array.from({ length: teamCount }, (_, seat) => ({
            room_id: room.id,
            seat,
            team: seat,
            ...(seat === 0 ? { occupant: 'human' as const, account_id: caller.id } : {}),
          })),
        )
        .execute();
      await publishPlay(trx, { t: 'room', roomId: room.id });
      return room.id;
    });
    if (settings.mapStatus === 'pending') await this.refreshPendingMaps({ roomId });
    return this.mustState(roomId);
  }

  async join(
    caller: Account,
    simVersion: SimVersion,
    code: string,
    regions: RegionRtt[] | undefined,
  ): Promise<RoomState> {
    const found = await this.db
      .selectFrom('rooms')
      .select(['id', 'status', 'sim_version', 'host_account_id'])
      .where('code', '=', normalizeCode(code))
      .executeTakeFirst();
    if (!found || found.status === 'closed') {
      throw apiError('not_found', 'This invite has expired or does not exist.');
    }
    if (found.sim_version !== simVersionKey(simVersion)) {
      throw apiError('update_required', 'This room is for another game version.', {
        simVersion: parseSimVersionKey(found.sim_version),
      });
    }
    const kick = await this.db
      .selectFrom('room_kicks')
      .select('until')
      .where('room_id', '=', found.id)
      .where('account_id', '=', caller.id)
      .where('until', '>', sql<Date>`now()`)
      .executeTakeFirst();
    if (kick) {
      throw apiError('forbidden', 'The host removed you from this room; try again later.', {
        until: kick.until.toISOString(),
      });
    }
    const already = await this.db
      .selectFrom('room_members')
      .select('account_id')
      .where('room_id', '=', found.id)
      .where('account_id', '=', caller.id)
      .executeTakeFirst();
    if (!already) {
      const subject = await accessSubject(this.db, caller.id);
      if (!subject) throw apiError('forbidden', 'Your account cannot join rooms.');
      const decision = await this.access.canJoin(subject, {
        roomId: found.id,
        hostAccountId: found.host_account_id,
        simVersion,
      });
      if (!decision.allowed) throw accessDenied(decision);
      await this.leaveOtherRooms(caller.id, found.id);
    }
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, found.id);
      if (room.status === 'closed') {
        throw apiError('not_found', 'This invite has expired or does not exist.');
      }
      if (!already) {
        const count = await trx
          .selectFrom('room_members')
          .select(sql<number>`count(*)::int`.as('n'))
          .where('room_id', '=', room.id)
          .executeTakeFirstOrThrow();
        if (count.n >= ROOM_RULES.maxMembers) throw apiError('conflict', 'The room is full.');
      }
      await trx
        .insertInto('room_members')
        .values({
          room_id: room.id,
          account_id: caller.id,
          region_rtts: JSON.stringify(regions ?? []),
        })
        .onConflict((oc) =>
          oc.columns(['room_id', 'account_id']).doUpdateSet({
            connected: true,
            last_seen_at: sql<Date>`now()`,
            ...(regions ? { region_rtts: JSON.stringify(regions) } : {}),
          }),
        )
        .execute();
      await this.bump(trx, room.id);
    });
    return this.mustState(found.id);
  }

  async leave(accountId: string, roomId: string): Promise<void> {
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      await this.requireMember(trx, roomId, accountId);
      if (room.status === 'closed') return;
      if (room.host_account_id === accountId && ROOM_RULES.hostLeaveCloses) {
        await this.closeLocked(trx, roomId, 'host_closed');
        return;
      }
      await trx
        .deleteFrom('room_members')
        .where('room_id', '=', roomId)
        .where('account_id', '=', accountId)
        .execute();
      await trx
        .updateTable('room_seats')
        .set({ occupant: 'open', account_id: null, ready: false })
        .where('room_id', '=', roomId)
        .where('account_id', '=', accountId)
        .execute();
      await this.bump(trx, roomId);
    });
  }

  /**
   * The host removes a member from an open room: their seat opens, they get
   * room.closed {reason: 'kicked'}, and they cannot rejoin for
   * ROOM_RULES.kickBanSeconds.
   */
  async kick(caller: Account, roomId: string, accountId: string): Promise<RoomState> {
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      await this.requireMember(trx, roomId, caller.id);
      if (room.host_account_id !== caller.id) {
        throw apiError('forbidden', 'Only the host can remove players.');
      }
      if (accountId === caller.id) throw apiError('bad_request', 'Leave the room instead.');
      if (room.status !== 'open') {
        throw apiError(
          'conflict',
          'Players cannot be removed while a match is starting or running.',
        );
      }
      const removed = await trx
        .deleteFrom('room_members')
        .where('room_id', '=', roomId)
        .where('account_id', '=', accountId)
        .executeTakeFirst();
      if (removed.numDeletedRows === 0n)
        throw apiError('not_found', 'That player is not in this room.');
      await trx
        .updateTable('room_seats')
        .set({ occupant: 'open', account_id: null, ready: false })
        .where('room_id', '=', roomId)
        .where('account_id', '=', accountId)
        .execute();
      const until = sql<Date>`now() + make_interval(secs => ${ROOM_RULES.kickBanSeconds})`;
      await trx
        .insertInto('room_kicks')
        .values({ room_id: roomId, account_id: accountId, kicked_by_account_id: caller.id, until })
        .onConflict((oc) =>
          oc.columns(['room_id', 'account_id']).doUpdateSet({
            until,
            kicked_by_account_id: caller.id,
            created_at: sql<Date>`now()`,
          }),
        )
        .execute();
      await this.bump(trx, roomId);
      await publishPlay(trx, {
        t: 'roomClosed',
        roomId,
        reason: 'kicked',
        accountIds: [accountId],
      });
    });
    return this.mustState(roomId);
  }

  async update(
    caller: Account,
    roomId: string,
    revision: number,
    changes: {
      name?: string;
      visibility?: 'public' | 'link';
      map?: RoomMapSelection;
      teams?: SetupTeam[];
      rules?: MatchRules;
      experiments?: string[];
    },
  ): Promise<RoomState> {
    const current = await this.db
      .selectFrom('rooms')
      .select(['sim_version', 'host_account_id'])
      .where('id', '=', roomId)
      .executeTakeFirst();
    if (!current) throw apiError('not_found', 'No such room.');
    if (current.host_account_id !== caller.id) {
      throw apiError('forbidden', 'Only the host can change the room.');
    }
    // Resolve outside the row lock: it may start a generation job.
    const resolution = changes.map
      ? await this.resolveMap(changes.map, current.sim_version, caller.id)
      : undefined;
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      if (room.host_account_id !== caller.id) {
        throw apiError('forbidden', 'Only the host can change the room.');
      }
      if (room.status !== 'open') throw apiError('conflict', 'The room is starting a match.');
      if (room.revision !== revision) {
        throw apiError('conflict', 'The room changed; apply your change to the latest state.', {
          revision: room.revision,
        });
      }
      let settings = room.settings as unknown as RoomSettings;
      let clearReady = false;
      const updates: { name?: string; visibility?: 'public' | 'link' } = {};
      if (changes.name !== undefined) updates.name = changes.name;
      if (changes.visibility !== undefined) updates.visibility = changes.visibility;
      if (Object.keys(updates).length > 0) {
        await trx.updateTable('rooms').set(updates).where('id', '=', roomId).execute();
      }
      if (resolution) {
        await this.applyMap(trx, roomId, settings, resolution);
        settings = (await this.lock(trx, roomId)).settings as unknown as RoomSettings;
        clearReady = true;
      }
      if (changes.teams) {
        const teams = changes.teams;
        const valid =
          teams.length === settings.teams.length && teams.every((team, i) => team.team === i);
        if (!valid) {
          throw apiError(
            'bad_request',
            `teams must list the map's ${settings.teams.length} teams in order 0..${settings.teams.length - 1}.`,
          );
        }
        settings = { ...settings, teams };
        clearReady = true;
      }
      if (changes.rules) {
        settings = { ...settings, rules: changes.rules };
        clearReady = true;
      }
      if (changes.experiments) {
        settings = { ...settings, experiments: changes.experiments };
        clearReady = true;
      }
      await this.writeSettings(trx, roomId, settings);
      if (clearReady && ROOM_RULES.settingsClearReady) await this.clearReady(trx, roomId);
      await this.bump(trx, roomId);
    });
    if (resolution?.status === 'pending') await this.refreshPendingMaps({ roomId });
    return this.mustState(roomId);
  }

  async setSeat(
    caller: Account,
    roomId: string,
    seatIndex: number,
    occupant:
      | { kind: 'self' }
      | { kind: 'open' }
      | { kind: 'locked' }
      | { kind: 'ai'; ai: AiId; name?: string },
  ): Promise<RoomState> {
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      await this.requireMember(trx, roomId, caller.id);
      if (room.status !== 'open') throw apiError('conflict', 'The room is starting a match.');
      const isHost = room.host_account_id === caller.id;
      const seat = (await trx
        .selectFrom('room_seats')
        .selectAll()
        .where('room_id', '=', roomId)
        .where('seat', '=', seatIndex)
        .executeTakeFirst()) as SeatRow | undefined;
      if (!seat) throw apiError('not_found', `The room has no seat ${seatIndex}.`);
      const setSeat = (values: {
        occupant?: 'open' | 'human' | 'ai';
        account_id?: string | null;
        ai_id?: string | null;
        ai_name?: string | null;
        ready?: boolean;
        locked?: boolean;
      }) =>
        trx
          .updateTable('room_seats')
          .set(values)
          .where('room_id', '=', roomId)
          .where('seat', '=', seatIndex)
          .execute();

      switch (occupant.kind) {
        case 'self': {
          if (!isHost && !ROOM_RULES.membersChooseSeats) {
            throw apiError('forbidden', 'Only the host assigns seats in this room.');
          }
          if (seat.account_id === caller.id) break;
          if (seat.occupant !== 'open' || seat.locked) {
            throw apiError('conflict', 'That seat is not open.');
          }
          await trx
            .updateTable('room_seats')
            .set({ occupant: 'open', account_id: null, ready: false })
            .where('room_id', '=', roomId)
            .where('account_id', '=', caller.id)
            .execute();
          await setSeat({ occupant: 'human', account_id: caller.id, ready: false });
          break;
        }
        case 'open': {
          if (seat.account_id === caller.id) {
            await setSeat({ occupant: 'open', account_id: null, ready: false });
            break;
          }
          if (!isHost) throw apiError('forbidden', 'Only the host can empty other seats.');
          await setSeat({
            occupant: 'open',
            account_id: null,
            ai_id: null,
            ai_name: null,
            ready: false,
            locked: false,
          });
          break;
        }
        case 'locked': {
          if (!isHost) throw apiError('forbidden', 'Only the host can lock seats.');
          if (seat.occupant === 'human') {
            throw apiError('conflict', 'Empty the seat before locking it.');
          }
          await setSeat({
            occupant: 'open',
            ai_id: null,
            ai_name: null,
            ready: false,
            locked: true,
          });
          break;
        }
        case 'ai': {
          if (!isHost) throw apiError('forbidden', 'Only the host can add AIs.');
          if (seat.occupant === 'human') {
            throw apiError('conflict', 'Empty the seat before putting an AI in it.');
          }
          if (occupant.ai === 'none') throw apiError('bad_request', 'Choose an AI.');
          await setSeat({
            occupant: 'ai',
            ai_id: occupant.ai,
            ai_name: occupant.name ?? aiDisplayName(occupant.ai),
            ready: false,
            locked: false,
          });
          break;
        }
      }
      await this.bump(trx, roomId);
    });
    return this.mustState(roomId);
  }

  async setReady(caller: Account, roomId: string, ready: boolean): Promise<RoomState> {
    await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      await this.requireMember(trx, roomId, caller.id);
      if (room.status !== 'open') throw apiError('conflict', 'The room is starting a match.');
      const updated = await trx
        .updateTable('room_seats')
        .set({ ready })
        .where('room_id', '=', roomId)
        .where('account_id', '=', caller.id)
        .executeTakeFirst();
      if (updated.numUpdatedRows === 0n) throw apiError('conflict', 'Take a seat first.');
      await this.bump(trx, roomId);
    });
    return this.mustState(roomId);
  }

  async chat(caller: Account, roomId: string, text: string): Promise<RoomChatMessage> {
    // Read the mute fresh: a moderator may have muted the account after the
    // socket signed in.
    const current = await this.db
      .selectFrom('accounts')
      .select(['muted_until', 'status'])
      .where('id', '=', caller.id)
      .executeTakeFirst();
    if (!current || current.status !== 'active') throw apiError('forbidden', 'Account inactive.');
    if (current.muted_until && current.muted_until > new Date()) {
      throw apiError('forbidden', 'You are muted.', {
        mutedUntil: current.muted_until.toISOString(),
      });
    }
    const messageId = await this.db.transaction().execute(async (trx) => {
      const room = await trx
        .selectFrom('rooms')
        .select('status')
        .where('id', '=', roomId)
        .executeTakeFirst();
      if (!room || room.status === 'closed') throw apiError('not_found', 'No such room.');
      await this.requireMember(trx, roomId, caller.id);
      const row = await trx
        .insertInto('room_chat_messages')
        .values({ room_id: roomId, account_id: caller.id, text })
        .returning('id')
        .executeTakeFirstOrThrow();
      await publishPlay(trx, { t: 'roomChat', roomId, messageId: row.id });
      return row.id;
    });
    return must(await this.chatMessage(messageId), 'chat message');
  }

  /**
   * Starts the room's match: checks readiness and access, records the match
   * with a platform-chosen seed, places it on a relay and tells every seated
   * player (match.start, with their ticket, from the replica holding their
   * socket).
   */
  async start(caller: Account, roomId: string): Promise<{ matchId: string }> {
    const prepared = await this.db.transaction().execute(async (trx) => {
      const room = await this.lock(trx, roomId);
      await this.requireMember(trx, roomId, caller.id);
      if (room.host_account_id !== caller.id) {
        throw apiError('forbidden', 'Only the host can start the match.');
      }
      if (room.status !== 'open') throw apiError('conflict', 'The room is already starting.');
      const settings = room.settings as unknown as RoomSettings;
      if (!settings.map) throw apiError('conflict', 'Choose a map first.');
      if (settings.mapStatus !== 'ready' || !settings.map.hash) {
        throw apiError(
          'conflict',
          settings.mapStatus === 'failed'
            ? `The map cannot be used: ${settings.mapProblem ?? 'generation failed'}.`
            : 'The map is not ready yet.',
        );
      }
      const seats = (await trx
        .selectFrom('room_seats')
        .selectAll()
        .where('room_id', '=', roomId)
        .orderBy('seat')
        .execute()) as SeatRow[];
      const occupied = seats.filter((s) => s.occupant !== 'open');
      if (occupied.length < ROOM_RULES.minOccupiedSeats) {
        throw apiError('conflict', `At least ${ROOM_RULES.minOccupiedSeats} seats must be taken.`);
      }
      const unready = seats.filter(
        (s) =>
          s.occupant === 'human' &&
          !s.ready &&
          !(ROOM_RULES.hostImplicitlyReady && s.account_id === room.host_account_id),
      );
      if (unready.length > 0) {
        throw apiError('conflict', 'Every seated player must be ready.', {
          seats: unready.map((s) => s.seat),
        });
      }
      await trx.updateTable('rooms').set({ status: 'starting' }).where('id', '=', roomId).execute();
      await this.bump(trx, roomId);
      return { room, settings, seats };
    });

    const { room, settings, seats } = prepared;
    try {
      const simVersion = storedSimVersion(room.sim_version);
      const humans = seats.flatMap((s) =>
        s.occupant === 'human' && s.account_id ? [{ ...s, account_id: s.account_id }] : [],
      );
      for (const seat of humans) {
        const subject = await accessSubject(this.db, seat.account_id);
        if (!subject) throw new StartError('access_denied', 'A seated player cannot play.');
        const decision =
          seat.account_id === room.host_account_id
            ? await this.access.canHost(subject, { simVersion, visibility: room.visibility })
            : await this.access.canJoin(subject, {
                roomId,
                hostAccountId: room.host_account_id,
                simVersion,
              });
        if (!decision.allowed) {
          throw new StartError('access_denied', decision.reason, {
            seat: seat.seat,
            ...(decision.requiredEntitlement
              ? { requiredEntitlement: decision.requiredEntitlement }
              : {}),
          });
        }
      }
      const names = new Map(
        humans.length === 0
          ? []
          : (
              await this.db
                .selectFrom('accounts')
                .select(['id', 'display_name'])
                .where(
                  'id',
                  'in',
                  humans.map((s) => s.account_id),
                )
                .execute()
            ).map((a) => [a.id, a.display_name]),
      );
      const setup: MatchSetup = {
        schemaVersion: 1,
        simVersion,
        seed: randomInt(0, 2 ** 32),
        map: mapSource(must(settings.map, 'room map')),
        teams: settings.teams,
        seats: seats.map((s): Seat => {
          if (s.occupant === 'human' && s.account_id) {
            return {
              seat: s.seat,
              kind: 'human',
              team: s.team,
              name: truncateUtf8(names.get(s.account_id) ?? `Player ${s.seat + 1}`),
              accountId: s.account_id,
            };
          }
          if (s.occupant === 'ai' && s.ai_id) {
            return {
              seat: s.seat,
              kind: 'ai',
              team: s.team,
              name: truncateUtf8(s.ai_name ?? aiDisplayName(s.ai_id as AiId)),
              ai: s.ai_id as AiId,
            };
          }
          // An empty (or locked) seat's colony stays on the map without a player.
          return { seat: s.seat, kind: 'ai', team: s.team, name: 'Nobody', ai: 'none' };
        }),
        rules: settings.rules,
        experiments: settings.experiments,
      };
      const probes = await this.db
        .selectFrom('room_members')
        .select('region_rtts')
        .where('room_id', '=', roomId)
        .where(
          'account_id',
          'in',
          humans.map((s) => s.account_id),
        )
        .execute();
      const created = await createMatch(this.db, {
        setup,
        origin: 'room',
        roomId,
        rated: false,
        placement: { players: probes.map((p) => p.region_rtts as unknown as RegionRtt[]) },
      });
      await this.db.transaction().execute(async (trx) => {
        await trx
          .updateTable('rooms')
          .set({ status: 'in_match', match_id: created.matchId })
          .where('id', '=', roomId)
          .where('status', '=', 'starting')
          .execute();
        await this.bump(trx, roomId);
        await publishPlay(trx, { t: 'matchStart', matchId: created.matchId });
      });
      this.logger.info(
        { room: roomId, match: created.matchId, relay: created.relay.id },
        'room match placed',
      );
      return { matchId: created.matchId };
    } catch (error) {
      await this.db.transaction().execute(async (trx) => {
        const reverted = await trx
          .updateTable('rooms')
          .set({ status: 'open' })
          .where('id', '=', roomId)
          .where('status', '=', 'starting')
          .executeTakeFirst();
        if (reverted.numUpdatedRows > 0n) await this.bump(trx, roomId);
      });
      if (error instanceof StartError) throw apiError(error.code, error.message, error.details);
      throw error;
    }
  }

  // --------------------------------------------------------------- presence

  /** Marks the account connected in its rooms; returns rooms that changed. */
  async markConnected(accountId: string): Promise<void> {
    await this.db.transaction().execute(async (trx) => {
      const rows = await trx
        .updateTable('room_members')
        .set({ connected: true, last_seen_at: sql<Date>`now()` })
        .where('account_id', '=', accountId)
        .where('connected', '=', false)
        .returning('room_id')
        .execute();
      for (const row of rows) await this.bump(trx, row.room_id);
    });
  }

  async markDisconnected(accountId: string): Promise<void> {
    await this.db.transaction().execute(async (trx) => {
      const rows = await trx
        .updateTable('room_members')
        .set({ connected: false, last_seen_at: sql<Date>`now()` })
        .where('account_id', '=', accountId)
        .where('connected', '=', true)
        .returning('room_id')
        .execute();
      for (const row of rows) await this.bump(trx, row.room_id);
    });
  }

  /**
   * Closes open rooms whose host has been gone too long or that sat idle,
   * and removes long-disconnected members. Guarded updates make it safe on
   * every replica at once.
   */
  async sweep(): Promise<{ closed: number; removed: number }> {
    const stale = await this.db
      .selectFrom('rooms as r')
      .leftJoin('room_members as h', (join) =>
        join.onRef('h.room_id', '=', 'r.id').onRef('h.account_id', '=', 'r.host_account_id'),
      )
      .select('r.id')
      .where('r.status', '=', 'open')
      .where((eb) =>
        eb.or([
          eb('h.account_id', 'is', null),
          eb.and([
            eb('h.connected', '=', false),
            eb(
              'h.last_seen_at',
              '<',
              sql<Date>`now() - make_interval(secs => ${ROOM_RULES.hostGraceSeconds})`,
            ),
          ]),
          eb(
            'r.updated_at',
            '<',
            sql<Date>`now() - make_interval(secs => ${ROOM_RULES.idleSeconds})`,
          ),
        ]),
      )
      .execute();
    let closed = 0;
    for (const { id } of stale) {
      await this.db.transaction().execute(async (trx) => {
        const room = await this.lock(trx, id);
        if (room.status !== 'open') return;
        await this.closeLocked(trx, id, 'expired');
        closed++;
      });
    }
    const gone = await this.db
      .selectFrom('room_members as m')
      .innerJoin('rooms as r', 'r.id', 'm.room_id')
      .select(['m.room_id', 'm.account_id'])
      .where('r.status', '=', 'open')
      .whereRef('m.account_id', '!=', 'r.host_account_id')
      .where('m.connected', '=', false)
      .where(
        'm.last_seen_at',
        '<',
        sql<Date>`now() - make_interval(secs => ${ROOM_RULES.memberGraceSeconds})`,
      )
      .execute();
    for (const member of gone) {
      await this.db.transaction().execute(async (trx) => {
        const deleted = await trx
          .deleteFrom('room_members')
          .where('room_id', '=', member.room_id)
          .where('account_id', '=', member.account_id)
          .where('connected', '=', false)
          .executeTakeFirst();
        if (deleted.numDeletedRows === 0n) return;
        await trx
          .updateTable('room_seats')
          .set({ occupant: 'open', account_id: null, ready: false })
          .where('room_id', '=', member.room_id)
          .where('account_id', '=', member.account_id)
          .execute();
        await this.bump(trx, member.room_id);
        await publishPlay(trx, {
          t: 'roomClosed',
          roomId: member.room_id,
          reason: 'expired',
          accountIds: [member.account_id],
        });
      });
    }
    await this.db
      .deleteFrom('room_kicks')
      .where('until', '<=', sql<Date>`now()`)
      .execute();
    return { closed, removed: gone.length };
  }
}

/** The MatchSetup map source of a ready room map. */
export function mapSource(selection: RoomMapSelection): MatchSetup['map'] {
  if (selection.kind === 'catalog') {
    return {
      kind: 'catalog',
      hash: selection.hash,
      ...(selection.mapId ? { mapId: selection.mapId } : {}),
    };
  }
  if (selection.kind === 'upload') {
    return { kind: 'upload', format: selection.format, hash: selection.hash };
  }
  if (!selection.hash) throw new Error('generated map has no hash yet');
  return { kind: 'generated', generator: selection.generator, hash: selection.hash };
}

/** Keeps existing alliances; new teams start on their own alliance. */
export function resizeTeams(teams: readonly SetupTeam[], count: number): SetupTeam[] {
  return Array.from({ length: count }, (_, team) => ({
    team,
    alliance: Math.min(teams[team]?.alliance ?? team, count - 1),
  }));
}

function accessDenied(decision: { reason: string; requiredEntitlement?: string }) {
  return apiError(
    'access_denied',
    decision.reason,
    decision.requiredEntitlement
      ? { requiredEntitlement: decision.requiredEntitlement }
      : undefined,
  );
}
