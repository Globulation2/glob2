// REST resource shapes for /api/v1. Accounts and auth are complete for M3;
// rooms, matches, maps and leaderboards define the shapes later milestones
// fill in. Server-emitted resources use Open objects (unknown fields allowed);
// request bodies are Strict.
import { Type, type Static, type TSchema } from 'typebox';
import {
  ClientPlatform,
  Cursor,
  DisplayName,
  HttpsOrWssUrl,
  InviteCode,
  Open,
  SeatIndex,
  Sha256Hex,
  Strict,
  TeamIndex,
  Timestamp,
  Uuid,
} from './common.ts';
import { AiId, GeneratorDescriptor, MatchRules, MatchSetup, SetupTeam } from './matchSetup.ts';
import { SimVersion } from './simVersion.ts';

/** A page of results with an opaque continuation cursor. */
export function Page<T extends TSchema>(item: T, description?: string) {
  return Open(
    { items: Type.Array(item), nextCursor: Type.Optional(Cursor) },
    description ? { description } : {},
  );
}

// ---------------------------------------------------------------- accounts

export const AccountKind = Type.Union([Type.Literal('guest'), Type.Literal('registered')]);
export const AccountRole = Type.Union([
  Type.Literal('user'),
  Type.Literal('moderator'),
  Type.Literal('admin'),
]);
export const AccountStatus = Type.Union([
  Type.Literal('active'),
  Type.Literal('banned'),
  Type.Literal('deleted'),
]);

export const PublicAccount = Open(
  {
    id: Uuid,
    displayName: DisplayName,
    kind: AccountKind,
    createdAt: Timestamp,
  },
  { description: 'What anyone may see about an account.' },
);
export type PublicAccount = Static<typeof PublicAccount>;

export const LinkedIdentity = Open({
  provider: Type.String({ maxLength: 64, description: 'Provider id from instance config.' }),
  linkedAt: Timestamp,
  email: Type.Optional(Type.String({ maxLength: 320 })),
});

export const SelfAccount = Open(
  {
    id: Uuid,
    displayName: DisplayName,
    kind: AccountKind,
    createdAt: Timestamp,
    role: AccountRole,
    status: AccountStatus,
    mutedUntil: Type.Optional(Timestamp),
    identities: Type.Array(LinkedIdentity),
    entitlements: Type.Array(Type.String({ maxLength: 64 })),
    canRename: Type.Optional(
      Type.Boolean({ description: 'False for guests: they get generated names.' }),
    ),
    /** Earliest time of the next rename; absent when a rename is allowed now. */
    renameAvailableAt: Type.Optional(Timestamp),
  },
  { description: 'The signed-in account as its owner sees it.' },
);
export type SelfAccount = Static<typeof SelfAccount>;

export const AuthTokens = Open(
  {
    tokenType: Type.Literal('Bearer'),
    accessToken: Type.String({ description: 'EdDSA JWT, short-lived.' }),
    accessTokenExpiresAt: Timestamp,
    refreshToken: Type.String({ description: 'Opaque; rotated on every use.' }),
    refreshTokenExpiresAt: Timestamp,
  },
  { description: 'Access and refresh tokens.' },
);
export type AuthTokens = Static<typeof AuthTokens>;

/** 256-bit secret, base64url without padding. */
export const DeviceCredential = Type.String({ pattern: '^[A-Za-z0-9_-]{43}$' });

/** POST /api/v1/auth/guest */
export const GuestSignInRequest = Strict(
  {
    deviceCredential: Type.Optional(DeviceCredential),
    platform: ClientPlatform,
  },
  {
    description:
      'Signs in a guest. Without a device credential a new guest account (with a generated Guest-NNNN name) is created and its credential returned once.',
  },
);
export type GuestSignInRequest = Static<typeof GuestSignInRequest>;

export const SignInResponse = Open(
  {
    account: SelfAccount,
    tokens: AuthTokens,
    deviceCredential: Type.Optional(DeviceCredential),
  },
  { description: 'Result of any sign-in. deviceCredential is present only for a new guest.' },
);
export type SignInResponse = Static<typeof SignInResponse>;

/** POST /api/v1/auth/refresh */
export const RefreshRequest = Strict({
  refreshToken: Type.String({ minLength: 1, maxLength: 512 }),
});
/** POST /api/v1/auth/sign-out */
export const SignOutRequest = Strict({
  refreshToken: Type.String({ minLength: 1, maxLength: 512 }),
});
/** PATCH /api/v1/accounts/me */
export const UpdateAccountRequest = Strict({ displayName: DisplayName });

/** Local usernames: lowercase letters, digits, '.', '_' and '-'; compared case-insensitively. */
export const LocalUsername = Type.String({ pattern: '^[A-Za-z0-9._-]{3,32}$' });
export const LocalPassword = Type.String({ minLength: 10, maxLength: 256 });

/**
 * POST /api/v1/auth/local/register (only when auth.local.enabled). With a guest
 * bearer token the guest is upgraded in place; otherwise a new account is created.
 */
export const LocalRegisterRequest = Strict({
  username: LocalUsername,
  password: LocalPassword,
  displayName: Type.Optional(DisplayName),
  platform: ClientPlatform,
});
export type LocalRegisterRequest = Static<typeof LocalRegisterRequest>;

/** POST /api/v1/auth/local/sign-in */
export const LocalSignInRequest = Strict({
  username: LocalUsername,
  password: Type.String({ minLength: 1, maxLength: 256 }),
  platform: ClientPlatform,
});
export type LocalSignInRequest = Static<typeof LocalSignInRequest>;

/**
 * `details` of a `conflict` error when a sign-in identity is already linked to
 * another account. Accounts are never merged: the client may offer to sign in
 * as that account instead.
 */
export const IdentityConflict = Open({
  reason: Type.Literal('identity_in_use'),
  provider: Type.String({ maxLength: 64 }),
  account: PublicAccount,
});
export type IdentityConflict = Static<typeof IdentityConflict>;

export const AuthProviderInfo = Open({
  id: Type.String({ maxLength: 64 }),
  kind: Type.Union([Type.Literal('oidc'), Type.Literal('apple'), Type.Literal('local')]),
  displayName: Type.String({ maxLength: 64 }),
});

export const QueueInfo = Open({
  id: Type.String({ pattern: '^[a-z0-9][a-z0-9-]{0,31}$' }),
  name: Type.String({ maxLength: 64 }),
  mode: Type.Union([Type.Literal('1v1'), Type.Literal('2v2')]),
  rated: Type.Boolean(),
  aiBackfillSeconds: Type.Optional(Type.Integer({ minimum: 0 })),
  acceptSeconds: Type.Optional(
    Type.Integer({
      minimum: 0,
      description: 'Accept prompt length for all-human groups; 0 = none.',
    }),
  ),
});
export type QueueInfo = Static<typeof QueueInfo>;

/** GET /api/v1/instance — what a client needs before signing in. */
export const InstanceInfo = Open(
  {
    name: Type.String({ maxLength: 128 }),
    origin: HttpsOrWssUrl,
    realtimeUrl: HttpsOrWssUrl,
    supportedSimVersions: Type.Array(SimVersion),
    authProviders: Type.Array(AuthProviderInfo),
    queues: Type.Array(QueueInfo),
    guestsAllowed: Type.Boolean(),
  },
  { description: 'Public description of a platform instance.' },
);
export type InstanceInfo = Static<typeof InstanceInfo>;

// ------------------------------------------------------------------- rooms

export const RoomVisibility = Type.Union([Type.Literal('public'), Type.Literal('link')]);

export const RoomMapSelection = Type.Union(
  [
    Strict({ kind: Type.Literal('catalog'), hash: Sha256Hex, mapId: Type.Optional(Uuid) }),
    Strict({
      kind: Type.Literal('upload'),
      format: Type.Union([Type.Literal('map'), Type.Literal('save')]),
      hash: Sha256Hex,
      reteaming: Type.Optional(
        Type.Array(Strict({ name: DisplayName, team: TeamIndex, accountId: Type.Optional(Uuid) }), {
          maxItems: 12,
          description: 'For saves: which returning player takes which team.',
        }),
      ),
    }),
    Strict({
      kind: Type.Literal('generated'),
      generator: GeneratorDescriptor,
      hash: Type.Optional(Sha256Hex),
    }),
  ],
  {
    description:
      'Map chosen in a room. A generated map has a hash once its generation job finishes; a match cannot start before.',
  },
);
export type RoomMapSelection = Static<typeof RoomMapSelection>;

export const RoomSeat = Open({
  seat: SeatIndex,
  team: TeamIndex,
  occupant: Type.Union([
    Open({ kind: Type.Literal('open') }),
    Open({
      kind: Type.Literal('human'),
      accountId: Uuid,
      displayName: DisplayName,
      ready: Type.Boolean(),
    }),
    Open({ kind: Type.Literal('ai'), ai: AiId, name: DisplayName }),
  ]),
  locked: Type.Optional(
    Type.Boolean({ description: 'Closed by the host; plays as an inactive player if empty.' }),
  ),
});
export type RoomSeat = Static<typeof RoomSeat>;

export const RoomMember = Open({
  accountId: Uuid,
  displayName: DisplayName,
  kind: AccountKind,
  connected: Type.Boolean(),
  seat: Type.Optional(SeatIndex),
});

export const RoomStatus = Type.Union([
  Type.Literal('open'),
  Type.Literal('starting'),
  Type.Literal('in_match'),
  Type.Literal('closed'),
]);

export const RoomState = Open(
  {
    id: Uuid,
    code: InviteCode,
    inviteUrl: HttpsOrWssUrl,
    name: Type.String({ maxLength: 64 }),
    visibility: RoomVisibility,
    status: RoomStatus,
    hostAccountId: Uuid,
    simVersion: SimVersion,
    map: Type.Optional(RoomMapSelection),
    mapStatus: Type.Optional(
      Type.Union([Type.Literal('ready'), Type.Literal('pending'), Type.Literal('failed')], {
        description:
          'pending: a generated map is being generated (or an upload validated); failed: the map cannot be used (see mapProblem). A match can start only when ready.',
      }),
    ),
    mapProblem: Type.Optional(Type.String({ maxLength: 2000 })),
    teams: Type.Array(SetupTeam),
    seats: Type.Array(RoomSeat),
    rules: MatchRules,
    experiments: Type.Array(Type.String()),
    members: Type.Array(RoomMember),
    matchId: Type.Optional(Uuid),
    revision: Type.Integer({
      minimum: 0,
      description: 'Increases on every change; clients ignore stale room.state events.',
    }),
    createdAt: Timestamp,
  },
  { description: 'Full state of a room, as sent in room.state events.' },
);
export type RoomState = Static<typeof RoomState>;

export const RoomSummary = Open({
  id: Uuid,
  code: InviteCode,
  name: Type.String({ maxLength: 64 }),
  hostDisplayName: DisplayName,
  simVersion: SimVersion,
  status: RoomStatus,
  seatsTotal: Type.Integer({ minimum: 0 }),
  seatsTaken: Type.Integer({ minimum: 0 }),
  mapTitle: Type.Optional(Type.String({ maxLength: 128 })),
});

export const RoomChatMessage = Open({
  id: Uuid,
  roomId: Uuid,
  accountId: Uuid,
  displayName: DisplayName,
  text: Type.String({ maxLength: 500 }),
  sentAt: Timestamp,
});
export type RoomChatMessage = Static<typeof RoomChatMessage>;

/** GET /api/v1/rooms?simVersion=<key> — public open rooms for a sim version. */
export const RoomList = Page(RoomSummary, 'Public rooms that can be joined.');

/**
 * GET /api/v1/invites/{code} — what an invite link points at, for landing
 * pages and "Join by code" previews. Lookups are rate limited per address.
 */
export const InviteInfo = Open(
  {
    code: InviteCode,
    status: Type.Union([Type.Literal('open'), Type.Literal('in_match'), Type.Literal('closed')]),
    roomName: Type.String({ maxLength: 64 }),
    hostDisplayName: DisplayName,
    simVersion: SimVersion,
    seatsTotal: Type.Integer({ minimum: 0 }),
    seatsTaken: Type.Integer({ minimum: 0 }),
    inviteUrl: HttpsOrWssUrl,
  },
  { description: 'Public summary of the room behind an invite code.' },
);
export type InviteInfo = Static<typeof InviteInfo>;

// ----------------------------------------------------------------- uploads

export const UploadFormat = Type.Union([Type.Literal('map'), Type.Literal('save')]);

export const SavedPlayer = Open({
  name: Type.String({ maxLength: 64 }),
  team: TeamIndex,
  kind: Type.Union([Type.Literal('human'), Type.Literal('ai')]),
});
export type SavedPlayer = Static<typeof SavedPlayer>;

/**
 * POST /api/v1/uploads?format=map|save&simVersion=<key> (body: the raw bytes
 * clients load, application/octet-stream) and GET /api/v1/uploads/{id}. The
 * file is private to its owner and validated by an engine agent of that sim
 * version; a valid upload can be chosen as a room map with
 * {kind: "upload", format, hash: sha256}.
 */
export const MapUpload = Open(
  {
    id: Uuid,
    format: UploadFormat,
    sha256: Sha256Hex,
    size: Type.Integer({ minimum: 0 }),
    simVersion: SimVersion,
    status: Type.Union([Type.Literal('pending'), Type.Literal('valid'), Type.Literal('invalid')]),
    fileName: Type.Optional(Type.String({ maxLength: 255 })),
    map: Type.Optional(
      Open({
        width: Type.Integer({ minimum: 1 }),
        height: Type.Integer({ minimum: 1 }),
        teamCount: Type.Integer({ minimum: 1, maximum: 12 }),
      }),
    ),
    versionMinor: Type.Optional(Type.Integer({ minimum: 0 })),
    title: Type.Optional(Type.String({ maxLength: 128 })),
    players: Type.Optional(
      Type.Array(SavedPlayer, {
        maxItems: 12,
        description: 'Saves: the players in the file, for reteaming returning players.',
      }),
    ),
    reason: Type.Optional(Type.String({ maxLength: 2000, description: 'Why it is invalid.' })),
    downloadUrl: HttpsOrWssUrl,
    createdAt: Timestamp,
  },
  { description: 'An uploaded private map or save.' },
);
export type MapUpload = Static<typeof MapUpload>;

// ----------------------------------------------------------------- matches

export const MatchOutcome = Type.Union([
  Type.Literal('won'),
  Type.Literal('lost'),
  Type.Literal('unresolved'),
  Type.Literal('abandoned'),
]);

export const VerificationStatus = Type.Union([
  Type.Literal('pending'),
  Type.Literal('verified'),
  Type.Literal('diverged'),
  Type.Literal('unverifiable'),
  Type.Literal('not_applicable'),
]);

export const RatingChange = Open({
  ladder: Type.String({ maxLength: 64 }),
  before: Type.Number(),
  after: Type.Number(),
  provisional: Type.Boolean(),
});

export const MatchParticipant = Open({
  seat: SeatIndex,
  team: TeamIndex,
  kind: Type.Union([Type.Literal('human'), Type.Literal('ai')]),
  displayName: DisplayName,
  accountId: Type.Optional(Uuid),
  ai: Type.Optional(AiId),
  outcome: Type.Optional(MatchOutcome),
  disconnects: Type.Integer({ minimum: 0 }),
  rating: Type.Optional(RatingChange),
});

export const MatchSummary = Open(
  {
    id: Uuid,
    simVersion: SimVersion,
    origin: Type.Union([Type.Literal('room'), Type.Literal('queue')]),
    queueId: Type.Optional(Type.String()),
    rated: Type.Boolean(),
    status: Type.Union([
      Type.Literal('starting'),
      Type.Literal('running'),
      Type.Literal('ended'),
      Type.Literal('cancelled'),
    ]),
    verification: VerificationStatus,
    mapHash: Sha256Hex,
    mapTitle: Type.Optional(Type.String({ maxLength: 128 })),
    startedAt: Type.Optional(Timestamp),
    endedAt: Type.Optional(Timestamp),
    durationTicks: Type.Optional(Type.Integer({ minimum: 0 })),
    participants: Type.Array(MatchParticipant),
  },
  { description: 'One match in a history list.' },
);
export type MatchSummary = Static<typeof MatchSummary>;

export const MatchTeamStats = Open({
  team: TeamIndex,
  outcome: MatchOutcome,
  prestige: Type.Integer(),
  eliminatedTick: Type.Optional(Type.Integer({ minimum: 0 })),
  statistics: Type.Record(Type.String(), Type.Number(), {
    description: 'standard_statistics from the verifier result.json.',
  }),
  timeline: Type.Array(Type.Array(Type.Number()), {
    description: 'End-of-game statistics history rows (result.json teams[].history).',
  }),
});

export const MatchArtifactInfo = Open({
  kind: Type.Union([Type.Literal('record'), Type.Literal('replay'), Type.Literal('result')]),
  url: HttpsOrWssUrl,
  size: Type.Integer({ minimum: 0 }),
  sha256: Sha256Hex,
});

export const MatchDetail = Open(
  {
    match: MatchSummary,
    setup: MatchSetup,
    teams: Type.Array(MatchTeamStats),
    artifacts: Type.Array(MatchArtifactInfo),
  },
  { description: 'GET /api/v1/matches/{id}.' },
);
export type MatchDetail = Static<typeof MatchDetail>;

export const MatchList = Page(MatchSummary, 'Matches, newest first.');

// -------------------------------------------------------------------- maps

export const MapVisibility = Type.Union([
  Type.Literal('public'),
  Type.Literal('unlisted'),
  Type.Literal('private'),
]);

export const MapVersionInfo = Open({
  hash: Sha256Hex,
  size: Type.Integer({ minimum: 0 }),
  width: Type.Integer({ minimum: 1 }),
  height: Type.Integer({ minimum: 1 }),
  teamCount: Type.Integer({ minimum: 1, maximum: 12 }),
  minVersionMinor: Type.Integer({ minimum: 0 }),
  validation: Type.Union([Type.Literal('pending'), Type.Literal('valid'), Type.Literal('invalid')]),
  previewUrl: Type.Optional(HttpsOrWssUrl),
  downloadUrl: HttpsOrWssUrl,
  createdAt: Timestamp,
});
export type MapVersionInfo = Static<typeof MapVersionInfo>;

export const MapInfo = Open(
  {
    id: Uuid,
    owner: PublicAccount,
    title: Type.String({ minLength: 1, maxLength: 128 }),
    description: Type.String({ maxLength: 4000 }),
    visibility: MapVisibility,
    hidden: Type.Boolean({ description: 'Hidden by a moderator.' }),
    latestVersion: Type.Optional(MapVersionInfo),
    stats: Open({
      plays: Type.Integer({ minimum: 0 }),
      downloads: Type.Integer({ minimum: 0 }),
      likes: Type.Integer({ minimum: 0 }),
    }),
    createdAt: Timestamp,
    updatedAt: Timestamp,
  },
  { description: 'A catalog map.' },
);
export type MapInfo = Static<typeof MapInfo>;

export const MapList = Page(MapInfo, 'Catalog maps.');

/** POST /api/v1/maps; the bytes follow with PUT /api/v1/maps/{id}/versions. */
export const CreateMapRequest = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  description: Type.String({ maxLength: 4000 }),
  visibility: MapVisibility,
});

/** POST /api/v1/maps/{id}/reports */
export const MapReportRequest = Strict({
  reason: Type.Union([
    Type.Literal('broken'),
    Type.Literal('offensive'),
    Type.Literal('copyright'),
    Type.Literal('other'),
  ]),
  details: Type.String({ maxLength: 2000 }),
});

// ------------------------------------------------------------ leaderboards

export const RatedEntity = Type.Union([
  Open({ kind: Type.Literal('account'), account: PublicAccount }),
  Open({ kind: Type.Literal('ai'), ai: AiId, simVersion: SimVersion }),
]);

export const LeaderboardEntry = Open({
  rank: Type.Integer({ minimum: 1 }),
  entity: RatedEntity,
  rating: Type.Number({ description: 'Displayed rating: ordinal scaled around 1500.' }),
  mu: Type.Number(),
  sigma: Type.Number(),
  games: Type.Integer({ minimum: 0 }),
  wins: Type.Integer({ minimum: 0 }),
  provisional: Type.Boolean(),
});

export const LeaderboardPage = Open(
  {
    ladder: Type.String({ maxLength: 64, description: 'Queue id the ladder belongs to.' }),
    entries: Type.Array(LeaderboardEntry),
    nextCursor: Type.Optional(Cursor),
  },
  { description: 'GET /api/v1/leaderboards/{ladder}.' },
);
export type LeaderboardPage = Static<typeof LeaderboardPage>;

export type AccountKind = Static<typeof AccountKind>;
export type AccountRole = Static<typeof AccountRole>;
export type AccountStatus = Static<typeof AccountStatus>;
export type LinkedIdentity = Static<typeof LinkedIdentity>;
export type DeviceCredential = Static<typeof DeviceCredential>;
export type RefreshRequest = Static<typeof RefreshRequest>;
export type SignOutRequest = Static<typeof SignOutRequest>;
export type UpdateAccountRequest = Static<typeof UpdateAccountRequest>;
export type AuthProviderInfo = Static<typeof AuthProviderInfo>;
export type RoomVisibility = Static<typeof RoomVisibility>;
export type RoomMember = Static<typeof RoomMember>;
export type RoomStatus = Static<typeof RoomStatus>;
export type RoomSummary = Static<typeof RoomSummary>;
export type RoomList = Static<typeof RoomList>;
export type UploadFormat = Static<typeof UploadFormat>;
export type MatchOutcome = Static<typeof MatchOutcome>;
export type VerificationStatus = Static<typeof VerificationStatus>;
export type RatingChange = Static<typeof RatingChange>;
export type MatchParticipant = Static<typeof MatchParticipant>;
export type MatchTeamStats = Static<typeof MatchTeamStats>;
export type MatchArtifactInfo = Static<typeof MatchArtifactInfo>;
export type MatchList = Static<typeof MatchList>;
export type MapVisibility = Static<typeof MapVisibility>;
export type MapList = Static<typeof MapList>;
export type CreateMapRequest = Static<typeof CreateMapRequest>;
export type MapReportRequest = Static<typeof MapReportRequest>;
export type RatedEntity = Static<typeof RatedEntity>;
export type LeaderboardEntry = Static<typeof LeaderboardEntry>;
