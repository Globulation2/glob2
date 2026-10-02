// The realtime WebSocket (/realtime): JSON text frames carrying requests with
// correlation ids, their responses, and server-pushed events.
//
//   client → server  {"type":"request","id":"7","method":"room.join","params":{...}}
//   server → client  {"type":"response","id":"7","ok":true,"result":{...}}
//                    {"type":"response","id":"7","ok":false,"error":{"code":"not_found",...}}
//                    {"type":"event","event":"room.state","data":{...}}
//
// The first request on a socket must be `session.hello`. Ids are chosen by the
// client and must be unique among its in-flight requests.
import { Type, type Static, type TSchema } from 'typebox';
import {
  ClientPlatform,
  DisplayName,
  ErrorBody,
  HttpsOrWssUrl,
  InviteCode,
  Open,
  SeatIndex,
  Strict,
  Timestamp,
  Uuid,
} from './common.ts';
import { AiId, MatchRules, MatchSetup, SetupTeam } from './matchSetup.ts';
import {
  IdentityConflict,
  MatchSummary,
  RoomChatMessage,
  RoomMapSelection,
  RoomState,
  RoomVisibility,
  SelfAccount,
  SignInResponse,
} from './resources.ts';
import { RelayRegion } from './relay.ts';
import { SimVersion } from './simVersion.ts';

export const REALTIME_PROTOCOL_VERSION = 1;

export const RequestId = Type.String({ minLength: 1, maxLength: 64 });

const EmptyResult = Open({});

/** Measured round trips to relay regions; the platform places matches on the closest relay. */
const RegionRtts = Type.Array(
  Strict({ region: RelayRegion, rttMs: Type.Integer({ minimum: 0, maximum: 60000 }) }),
  { maxItems: 32, description: 'Measured round trip to each relay region.' },
);

const ExperimentKeys = Type.Array(Type.String({ pattern: '^[a-z0-9]+(-[a-z0-9]+)*$' }), {
  maxItems: 64,
  uniqueItems: true,
});

/**
 * The player's displayed rating on the match's ladder and what it would become
 * if their side won or lost, computed at the start with the same rating model
 * as the verified update. Rated matches only; a preview, not a promise.
 */
export const MatchRatingPreview = Open(
  {
    ladder: Type.String({ maxLength: 64 }),
    before: Type.Number(),
    ifWon: Type.Number(),
    ifLost: Type.Number(),
    provisional: Type.Boolean({ description: 'The rating before the match is provisional.' }),
  },
  { description: 'Rating preview of a rated match (MatchAssignment.ratingPreview).' },
);
export type MatchRatingPreview = Static<typeof MatchRatingPreview>;

/** The match a client has been placed in, with its ticket for the relay. */
export const MatchAssignment = Open(
  {
    matchId: Uuid,
    seat: SeatIndex,
    ticket: Type.String({ description: 'Match ticket JWT (see MatchTicketClaims).' }),
    ticketExpiresAt: Timestamp,
    relayUrl: HttpsOrWssUrl,
    setup: MatchSetup,
    mapUrl: HttpsOrWssUrl,
    mapTitle: Type.Optional(
      Type.String({
        maxLength: 128,
        description: 'Display name of the map (catalog title, or the generator name).',
      }),
    ),
    ratingPreview: Type.Optional(MatchRatingPreview),
  },
  { description: 'Everything a client needs to connect to the relay and load the match.' },
);
export type MatchAssignment = Static<typeof MatchAssignment>;

interface MethodContract {
  params: TSchema;
  result: TSchema;
  description: string;
}

/** Client → server requests. */
export const realtimeMethods = {
  'session.hello': {
    description: 'First message on a socket. Unsupported sim versions may still sign in.',
    params: Strict({
      protocol: Type.Literal(REALTIME_PROTOCOL_VERSION),
      client: Strict({
        platform: ClientPlatform,
        version: Type.String({ maxLength: 64 }),
        simVersion: SimVersion,
      }),
      accessToken: Type.Optional(Type.String({ maxLength: 4096 })),
    }),
    result: Open({
      sessionId: Uuid,
      serverTime: Timestamp,
      simSupported: Type.Boolean({
        description: 'False: rooms, queues and matches answer update_required.',
      }),
      account: Type.Optional(SelfAccount),
    }),
  },
  'session.authenticate': {
    description: 'Attach (or replace) the access token of the socket, e.g. after a refresh.',
    params: Strict({ accessToken: Type.String({ minLength: 1, maxLength: 4096 }) }),
    result: Open({ account: SelfAccount }),
  },
  'session.ping': {
    description:
      'Application-level keepalive for clients that cannot send WebSocket pings (browsers). The server also pings every socket.',
    params: Strict({}),
    result: Open({ serverTime: Timestamp }),
  },
  'auth.handoff.begin': {
    description:
      'Start a browser sign-in; the client opens signInUrl and shows confirmationCode. Completion arrives as auth.handoff.completed. On an authenticated socket the provider is linked to that account (a guest becomes registered).',
    params: Strict({
      provider: Type.Optional(Type.String({ maxLength: 64 })),
      mode: Type.Optional(
        Type.Union([Type.Literal('link'), Type.Literal('signin')], {
          description:
            'link (default on an authenticated socket): attach the identity to the current account. signin: switch to the account owning the identity, creating one if new.',
        }),
      ),
    }),
    result: Open({
      attemptId: Uuid,
      signInUrl: HttpsOrWssUrl,
      confirmationCode: Type.String({ pattern: '^[A-Z0-9]{4,12}$' }),
      expiresAt: Timestamp,
      resumeToken: Type.String({
        pattern: '^[A-Za-z0-9_-]{43}$',
        description:
          'Secret for auth.handoff.resume if the socket drops while the browser is open (common on phones). Keep it in memory only.',
      }),
    }),
  },
  'auth.handoff.resume': {
    description:
      'Re-attach a pending (or finished but undelivered) browser sign-in to this socket after a reconnect; its result then arrives as usual.',
    params: Strict({
      attemptId: Uuid,
      resumeToken: Type.String({ pattern: '^[A-Za-z0-9_-]{43}$' }),
    }),
    result: Open({
      status: Type.Union([Type.Literal('pending'), Type.Literal('finished')]),
    }),
  },
  'auth.handoff.cancel': {
    description: 'Abandon a pending browser sign-in.',
    params: Strict({ attemptId: Uuid }),
    result: EmptyResult,
  },
  'room.create': {
    description: 'Create a room hosted by the caller (AccessPolicy.canHost).',
    params: Strict({
      name: Type.String({ minLength: 1, maxLength: 64 }),
      visibility: RoomVisibility,
      map: Type.Optional(RoomMapSelection),
      rules: Type.Optional(MatchRules),
      experiments: Type.Optional(ExperimentKeys),
      regions: Type.Optional(RegionRtts),
    }),
    result: Open({ room: RoomState }),
  },
  'room.join': {
    description:
      "Join a room by invite code (AccessPolicy.canJoin). The room must be for the caller's sim version (update_required otherwise). Joining a room the caller is already in returns its state.",
    params: Strict({ code: InviteCode, regions: Type.Optional(RegionRtts) }),
    result: Open({ room: RoomState }),
  },
  'room.leave': {
    description: 'Leave a room.',
    params: Strict({ roomId: Uuid }),
    result: EmptyResult,
  },
  'room.update': {
    description:
      'Host changes room settings. revision must equal the current room revision (optimistic concurrency).',
    params: Strict({
      roomId: Uuid,
      revision: Type.Integer({ minimum: 0 }),
      changes: Strict({
        name: Type.Optional(Type.String({ minLength: 1, maxLength: 64 })),
        visibility: Type.Optional(RoomVisibility),
        map: Type.Optional(RoomMapSelection),
        teams: Type.Optional(Type.Array(SetupTeam, { minItems: 1, maxItems: 12 })),
        rules: Type.Optional(MatchRules),
        experiments: Type.Optional(ExperimentKeys),
      }),
    }),
    result: Open({ room: RoomState }),
  },
  'room.setSeat': {
    description:
      'self: take an open, unlocked seat (moving from the current one). open: leave your own seat, or (host) empty or unlock any seat. ai (host): put an AI in an open or AI seat. locked (host): close an empty seat so nobody takes it.',
    params: Strict({
      roomId: Uuid,
      seat: SeatIndex,
      occupant: Type.Union([
        Strict({ kind: Type.Literal('self') }),
        Strict({ kind: Type.Literal('open') }),
        Strict({ kind: Type.Literal('ai'), ai: AiId, name: Type.Optional(DisplayName) }),
        Strict({ kind: Type.Literal('locked') }),
      ]),
    }),
    result: Open({ room: RoomState }),
  },
  'room.kick': {
    description:
      'Host removes a member from an open room. The member receives room.closed {reason: "kicked"} and cannot rejoin that room for 10 minutes (room.join answers forbidden with details.until).',
    params: Strict({ roomId: Uuid, accountId: Uuid }),
    result: Open({ room: RoomState }),
  },
  'room.setReady': {
    description: 'Mark the caller ready or not ready.',
    params: Strict({ roomId: Uuid, ready: Type.Boolean() }),
    result: Open({ room: RoomState }),
  },
  'room.chat': {
    description: 'Send a chat message to the room.',
    params: Strict({ roomId: Uuid, text: Type.String({ minLength: 1, maxLength: 500 }) }),
    result: Open({ message: RoomChatMessage }),
  },
  'room.start': {
    description:
      'Host starts the match once every seated human is ready (the host counts as ready); every seated member then receives match.start.',
    params: Strict({ roomId: Uuid }),
    result: Open({ matchId: Uuid }),
  },
  'queue.join': {
    description: 'Enter a quick-match queue (AccessPolicy.canQueue).',
    params: Strict({
      queueId: Type.String({ pattern: '^[a-z0-9][a-z0-9-]{0,31}$' }),
      regions: RegionRtts,
      allowAiOpponent: Type.Optional(
        Type.Boolean({
          description:
            '"Allow an AI opponent": the queue may fill empty seats with AIs after its backfill delay. Default true.',
        }),
      ),
    }),
    result: Open({ ticketId: Uuid, joinedAt: Timestamp }),
  },
  'queue.respond': {
    description: 'Accept or decline a proposed ranked match (queue.proposal with requiresAccept).',
    params: Strict({ proposalId: Uuid, accept: Type.Boolean() }),
    result: EmptyResult,
  },
  'queue.leave': {
    description: 'Leave a queue.',
    params: Strict({ ticketId: Uuid }),
    result: EmptyResult,
  },
  'match.reconnect': {
    description:
      'Get a fresh ticket for a starting or running match the caller is seated in (also the way to fetch a missed match.start).',
    params: Strict({
      matchId: Uuid,
      relayUnavailable: Type.Optional(
        Type.Boolean({
          description:
            'The assigned relay refused the match as new (Reject 5: draining or full). While no relay has reported the match running, the platform moves it to another relay and sends every player a new match.start.',
        }),
      ),
    }),
    result: MatchAssignment,
  },
} as const satisfies Record<string, MethodContract>;

export type RealtimeMethod = keyof typeof realtimeMethods;
export type RealtimeParams<M extends RealtimeMethod> = Static<
  (typeof realtimeMethods)[M]['params']
>;
export type RealtimeResult<M extends RealtimeMethod> = Static<
  (typeof realtimeMethods)[M]['result']
>;

interface EventContract {
  data: TSchema;
  description: string;
}

/** Server → client events. */
export const realtimeEvents = {
  'session.revoked': {
    description: 'The session ended server-side (signed out elsewhere, banned, token revoked).',
    data: Open({ reason: Type.String({ maxLength: 200 }) }),
  },
  'auth.handoff.completed': {
    description:
      'A browser sign-in this socket started has completed. The socket is now authenticated as session.account.',
    data: Open({
      attemptId: Uuid,
      session: SignInResponse,
      linked: Type.Boolean({
        description:
          'True: the identity was linked to the account the socket was signed in as. False: the socket switched to (or signed in as) another account.',
      }),
    }),
  },
  'auth.handoff.failed': {
    description: 'A browser sign-in this socket started will not complete.',
    data: Open({
      attemptId: Uuid,
      reason: Type.Union([
        Type.Literal('expired'),
        Type.Literal('denied'),
        Type.Literal('cancelled'),
        Type.Literal('conflict'),
        Type.Literal('error'),
      ]),
      conflict: Type.Optional(IdentityConflict),
    }),
  },
  'room.state': {
    description: 'Full room state after any change; ignore revisions older than the last seen.',
    data: Open({ room: RoomState }),
  },
  'room.chat': {
    description: 'A chat message in a room the client is in.',
    data: Open({ message: RoomChatMessage }),
  },
  'room.closed': {
    description: 'The room was closed or the client was removed from it.',
    data: Open({
      roomId: Uuid,
      reason: Type.Union([
        Type.Literal('host_closed'),
        Type.Literal('kicked'),
        Type.Literal('expired'),
      ]),
    }),
  },
  'queue.status': {
    description: 'Periodic queue progress.',
    data: Open({
      ticketId: Uuid,
      queueId: Type.String(),
      waitedSeconds: Type.Integer({ minimum: 0 }),
      ratingWindow: Type.Optional(Type.Number({ minimum: 0 })),
      aiBackfillAt: Type.Optional(Timestamp),
    }),
  },
  'queue.proposal': {
    description:
      'The queue grouped the client with other players. With requiresAccept the client must answer queue.respond before expiresAt; otherwise the match starts without a prompt.',
    data: Open({
      proposalId: Uuid,
      ticketId: Uuid,
      queueId: Type.String(),
      requiresAccept: Type.Boolean(),
      expiresAt: Type.Optional(Timestamp),
      humans: Type.Integer({ minimum: 1, maximum: 12 }),
      ais: Type.Integer({ minimum: 0, maximum: 12 }),
    }),
  },
  'queue.proposalEnded': {
    description:
      'A proposal the client was in will not start. requeued: the ticket is waiting again at its original position; removed: the ticket left the queue (declined or did not answer) and joining again is blocked until cooldownUntil.',
    data: Open({
      proposalId: Uuid,
      ticketId: Uuid,
      outcome: Type.Union([Type.Literal('requeued'), Type.Literal('removed')]),
      reason: Type.Union([
        Type.Literal('declined'),
        Type.Literal('timeout'),
        Type.Literal('other_declined'),
        Type.Literal('start_failed'),
      ]),
      cooldownUntil: Type.Optional(Timestamp),
    }),
  },
  'queue.matchFound': {
    description: 'The queue placed the client in a match; match.start follows.',
    data: Open({ ticketId: Uuid, matchId: Uuid }),
  },
  'match.start': {
    description: 'Connect to the relay with the ticket and load the setup.',
    data: MatchAssignment,
  },
  'match.updated': {
    description: 'A match the client played changed (ended, verified, ratings applied).',
    data: Open({ match: MatchSummary }),
  },
} as const satisfies Record<string, EventContract>;

export type RealtimeEventName = keyof typeof realtimeEvents;
export type RealtimeEventData<E extends RealtimeEventName> = Static<
  (typeof realtimeEvents)[E]['data']
>;

/** Generic request envelope; params are checked against the method's own schema. */
export const RealtimeRequest = Strict(
  {
    type: Type.Literal('request'),
    id: RequestId,
    method: Type.String({ pattern: '^[a-z]+(\\.[A-Za-z]+)+$', maxLength: 64 }),
    params: Type.Object({}),
  },
  { description: 'Client request envelope.' },
);
export type RealtimeRequest = Static<typeof RealtimeRequest>;

export const RealtimeResponse = Type.Union(
  [
    Open({
      type: Type.Literal('response'),
      id: RequestId,
      ok: Type.Literal(true),
      result: Type.Object({}),
    }),
    Open({
      type: Type.Literal('response'),
      id: RequestId,
      ok: Type.Literal(false),
      error: ErrorBody,
    }),
  ],
  { description: 'Server response envelope, correlated to a request by id.' },
);
export type RealtimeResponse = Static<typeof RealtimeResponse>;

export const RealtimeEvent = Open(
  {
    type: Type.Literal('event'),
    event: Type.String({ pattern: '^[a-z]+(\\.[A-Za-z]+)+$', maxLength: 64 }),
    data: Type.Object({}),
  },
  { description: 'Server event envelope.' },
);
export type RealtimeEvent = Static<typeof RealtimeEvent>;

export const RealtimeServerMessage = Type.Union([RealtimeResponse, RealtimeEvent], {
  description: 'Any server → client frame.',
});
export type RealtimeServerMessage = Static<typeof RealtimeServerMessage>;

/** Schema-registry name for a method's params or result, e.g. RealtimeRoomSetSeatParams. */
export function realtimeSchemaName(name: string, part: 'Params' | 'Result' | 'Event'): string {
  const pascal = name
    .split('.')
    .map((piece) => piece.charAt(0).toUpperCase() + piece.slice(1))
    .join('');
  return part === 'Event' ? `RealtimeEvent${pascal}` : `Realtime${pascal}${part}`;
}

export type RequestId = Static<typeof RequestId>;
