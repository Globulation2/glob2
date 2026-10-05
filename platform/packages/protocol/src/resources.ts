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
import { AiId, GeneratorDescriptor, MatchRules, SetupTeam } from './matchSetup.ts';
import { SimVersion } from './simVersion.ts';
import { TeamTimelinePoint } from './jobs.ts';

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
    avatarUrl: Type.Optional(Type.String()),
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
    avatarUrl: Type.Optional(Type.String()),
    avatarSource: Type.Optional(
      Type.Union([Type.Literal('automatic'), Type.Literal('uploaded'), Type.Literal('initials')]),
    ),
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

/**
 * DELETE /api/v1/accounts/me: deletes the caller's account for good. The
 * caller types the account's current display name to confirm.
 */
export const DeleteAccountRequest = Strict({
  confirmDisplayName: Type.String({ minLength: 1, maxLength: 64 }),
});

/** `format` of an AccountExport; the suffix changes only for incompatible layouts. */
export const ACCOUNT_EXPORT_FORMAT = 'glob2-account-export/1';

const ExportRecord = Type.Record(Type.String(), Type.Unknown());
const ExportRows = Type.Array(ExportRecord);

/**
 * GET /api/v1/accounts/me/export ("download my data"): everything the instance
 * stores about the caller's account. Rows keep the database's columns in
 * camelCase, without nulls; secrets (password, credential and token hashes,
 * confirmation codes) and other people's ids are left out. See
 * docs/multiplayer/identity.md, "Downloading your data".
 */
export const AccountExport = Open(
  {
    format: Type.Literal(ACCOUNT_EXPORT_FORMAT),
    exportedAt: Timestamp,
    instance: Type.String({ description: 'Public origin of the instance.' }),
    account: ExportRecord,
    signIn: Open({
      identities: ExportRows,
      devices: ExportRows,
      refreshTokens: ExportRows,
      webSessions: ExportRows,
      signInAttempts: ExportRows,
    }),
    skins: Type.Optional(
      Open({
        published: ExportRows,
        equipment: ExportRows,
        drafts: ExportRows,
        matches: ExportRows,
        purchases: ExportRows,
        paymentEvents: ExportRows,
        reports: ExportRows,
      }),
    ),
    entitlements: ExportRows,
    moderation: Type.Array(ExportRecord, {
      description: 'Moderation actions about the account (not who took them).',
    }),
    ratings: ExportRows,
    ratingHistory: ExportRows,
    matches: Type.Array(ExportRecord, {
      description: 'Every match the account played, with its seat, result and rating change.',
    }),
    rooms: Open({
      hosted: ExportRows,
      memberships: ExportRows,
      seats: ExportRows,
      chat: ExportRows,
      kicks: ExportRows,
    }),
    matchmaking: Open({
      tickets: ExportRows,
      cooldowns: ExportRows,
      proposals: ExportRows,
    }),
    hive: Type.Optional(
      Open({
        wallets: ExportRows,
        ledger: ExportRows,
        calls: ExportRows,
        purchases: ExportRows,
        sessions: ExportRows,
        events: ExportRows,
        operations: ExportRows,
        programs: ExportRows,
      }),
    ),
    mapStudio: Type.Optional(
      Open({
        wallets: ExportRows,
        ledger: ExportRows,
        calls: ExportRows,
        purchases: ExportRows,
        threads: ExportRows,
        messages: ExportRows,
        requests: ExportRows,
        attempts: ExportRows,
        events: Type.Optional(ExportRows),
        artifacts: Type.Optional(ExportRows),
      }),
    ),
    ais: Type.Optional(
      Open({
        published: ExportRows,
        likes: ExportRows,
        favourites: ExportRows,
        reports: ExportRows,
        uploads: ExportRows,
        downloads: ExportRows,
      }),
    ),
    maps: Open({
      published: ExportRows,
      likes: ExportRows,
      reports: ExportRows,
      uploads: ExportRows,
      downloads: ExportRows,
    }),
  },
  { description: 'Everything the instance stores about the signed-in account.' },
);
export type AccountExport = Static<typeof AccountExport>;

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
  maps: Type.Optional(
    Type.Array(Type.String({ maxLength: 64 }), {
      maxItems: 64,
      description: 'Generator ids of the map pool, for display.',
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
    features: Type.Optional(
      Type.Array(Type.String({ maxLength: 64 }), {
        maxItems: 64,
        description:
          "Optional behaviours this instance supports, for clients that would otherwise send a request an older server rejects. 'queue.multi': queue.join takes queueIds (one search in several queues).",
      }),
    ),
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
    mapTitle: Type.Optional(
      Type.String({
        maxLength: 128,
        description:
          'Display name of a catalog or uploaded map (its catalog title, or the title read from the uploaded file). Absent for generated maps, which clients name from the generator.',
      }),
    ),
    teams: Type.Array(SetupTeam),
    seats: Type.Array(RoomSeat),
    rules: MatchRules,
    experiments: Type.Array(Type.String()),
    members: Type.Array(RoomMember),
    matchId: Type.Optional(Uuid),
    notice: Type.Optional(
      Type.String({
        maxLength: 500,
        description:
          'Shown to every member of an open room: why the last start did not happen (e.g. the server was interrupted while starting). Cleared by the next start.',
      }),
    ),
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
  // The catalog map's server preview (PNG), when the room uses one and it is ready.
  mapPreviewUrl: Type.Optional(HttpsOrWssUrl),
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

export const MatchOutcome = Type.Union(
  [
    Type.Literal('won'),
    Type.Literal('lost'),
    Type.Literal('draw'),
    Type.Literal('unresolved'),
    Type.Literal('abandoned'),
  ],
  {
    description:
      'draw: a win shared by teams of more than one alliance (a prestige or sudden-death tie at the top). Draws are not rated.',
  },
);

export const VerificationStatus = Type.Union(
  [
    Type.Literal('pending'),
    Type.Literal('verified'),
    Type.Literal('diverged'),
    Type.Literal('unverifiable'),
    Type.Literal('not_applicable'),
    Type.Literal('failed'),
  ],
  {
    description:
      'failed: the server could not run its check (the verify job failed or was lost); the match is not rated unless an operator re-runs verification. Clients that do not know a value treat it as pending.',
  },
);

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
    queueName: Type.Optional(
      Type.String({
        maxLength: 64,
        description: 'The queue as players know it ("Casual 1v1"), from the instance config.',
      }),
    ),
    rated: Type.Boolean(),
    status: Type.Union([
      Type.Literal('starting'),
      Type.Literal('running'),
      Type.Literal('ended'),
      Type.Literal('cancelled'),
    ]),
    endReason: Type.Optional(
      Type.Union([Type.Literal('completed'), Type.Literal('abandoned'), Type.Literal('aborted')], {
        description:
          'Why an ended match ended. aborted: the match was lost (for example with its relay) and changes no rating.',
      }),
    ),
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
  timeline: Type.Array(TeamTimelinePoint, {
    description: 'Team history from the verifier result (one sample per 512 ticks), oldest first.',
  }),
});

export const MatchArtifactInfo = Open({
  kind: Type.Union([Type.Literal('record'), Type.Literal('replay'), Type.Literal('result')]),
  url: HttpsOrWssUrl,
  size: Type.Integer({ minimum: 0 }),
  sha256: Sha256Hex,
});

// MatchDetail (GET /api/v1/matches/{id}) lives in history.ts.

export const MatchList = Page(MatchSummary, 'Matches, newest first.');

// -------------------------------------------------------------------- maps
//
// Map catalog (/api/v1/maps). Visibility: public maps are listed; unlisted
// maps (the default) are reachable by id or link; private maps only by their
// owner. Hidden maps (moderation) are seen only by their owner and moderators.

export const MapVisibility = Type.Union([
  Type.Literal('public'),
  Type.Literal('unlisted'),
  Type.Literal('private'),
]);
export type MapVisibility = Static<typeof MapVisibility>;

export const MapMadeWith = Type.Union([Type.Literal('hand'), Type.Literal('generator')], {
  description: 'How the owner says the map was made: in the editor, or by a map generator.',
});

const ValidationState = Type.Union([
  Type.Literal('pending'),
  Type.Literal('valid'),
  Type.Literal('invalid'),
]);

/**
 * One uploaded version of a catalog map. Map facts are present once an
 * engine agent validated it. Rooms choose it as {kind: "catalog", hash}.
 */
export const MapVersionInfo = Open({
  hash: Sha256Hex,
  size: Type.Integer({ minimum: 0 }),
  width: Type.Optional(Type.Integer({ minimum: 1 })),
  height: Type.Optional(Type.Integer({ minimum: 1 })),
  teamCount: Type.Optional(Type.Integer({ minimum: 1, maximum: 12 })),
  minVersionMinor: Type.Optional(
    Type.Integer({
      minimum: 0,
      description:
        'Format version the file was saved with: engines from this VERSION_MINOR load it.',
    }),
  ),
  simVersion: Type.Optional(SimVersion),
  validation: ValidationState,
  reason: Type.Optional(Type.String({ maxLength: 2000, description: 'Why it is invalid.' })),
  fileTitle: Type.Optional(
    Type.String({ maxLength: 128, description: 'Map name stored in the file.' }),
  ),
  notes: Type.Optional(Type.String({ maxLength: 2000 })),
  preview: Type.Union([Type.Literal('pending'), Type.Literal('ready'), Type.Literal('failed')]),
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
    hiddenReason: Type.Optional(
      Type.String({ maxLength: 2000, description: 'Shown to the owner and moderators only.' }),
    ),
    madeWith: MapMadeWith,
    authoring: Type.Optional(Open({ kind: Type.Literal('ai'), pipelineVersion: Type.String() })),
    generator: Type.Optional(GeneratorDescriptor),
    latestVersion: Type.Optional(MapVersionInfo),
    stats: Open({
      plays: Type.Integer({ minimum: 0, description: 'Ended matches played on any version.' }),
      downloads: Type.Integer({
        minimum: 0,
        description: 'Downloads, counted once per downloader and day.',
      }),
      likes: Type.Integer({ minimum: 0 }),
    }),
    createdAt: Timestamp,
    updatedAt: Timestamp,
  },
  { description: 'A catalog map.' },
);
export type MapInfo = Static<typeof MapInfo>;

/**
 * GET /api/v1/maps?owner=me|<accountId>&teams=&minSide=&maxSide=&madeWith=&q=&sort=recent|likes|plays|downloads&cursor=&limit=
 * Without owner: public maps with a valid version. owner=me: all of the caller's maps.
 */
export const MapList = Page(MapInfo, 'Catalog maps.');

/** GET /api/v1/maps/{id} */
export const MapDetail = Open(
  {
    map: MapInfo,
    versions: Type.Array(MapVersionInfo, {
      description: 'Newest first; pending and invalid versions only for the owner and moderators.',
    }),
    viewer: Open({
      owner: Type.Boolean(),
      moderator: Type.Boolean(),
      liked: Type.Boolean(),
      reported: Type.Boolean({ description: 'The caller has an open report on this map.' }),
    }),
  },
  { description: 'A catalog map with its versions.' },
);
export type MapDetail = Static<typeof MapDetail>;

/**
 * POST /api/v1/maps. The bytes follow with POST /api/v1/maps/{id}/versions
 * (application/octet-stream, the uncompressed file; ?simVersion=<key>, default
 * the newest the instance serves; &notes=).
 */
export const CreateMapRequest = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  description: Type.Optional(Type.String({ maxLength: 4000 })),
  visibility: Type.Optional(MapVisibility),
  madeWith: Type.Optional(MapMadeWith),
  generator: Type.Optional(GeneratorDescriptor),
});
export type CreateMapRequest = Static<typeof CreateMapRequest>;

/** PATCH /api/v1/maps/{id} (owner) */
export const UpdateMapRequest = Strict({
  title: Type.Optional(Type.String({ minLength: 1, maxLength: 128 })),
  description: Type.Optional(Type.String({ maxLength: 4000 })),
  visibility: Type.Optional(MapVisibility),
});
export type UpdateMapRequest = Static<typeof UpdateMapRequest>;

/** PUT and DELETE /api/v1/maps/{id}/like */
export const MapLikeResult = Open({ liked: Type.Boolean(), likes: Type.Integer({ minimum: 0 }) });
export type MapLikeResult = Static<typeof MapLikeResult>;

export const MapReportReason = Type.Union([
  Type.Literal('broken'),
  Type.Literal('offensive'),
  Type.Literal('copyright'),
  Type.Literal('other'),
]);
export const MapReportStatus = Type.Union([
  Type.Literal('open'),
  Type.Literal('resolved'),
  Type.Literal('dismissed'),
]);

/** POST /api/v1/maps/{id}/reports */
export const MapReportRequest = Strict({
  reason: MapReportReason,
  details: Type.String({ maxLength: 2000 }),
});
export type MapReportRequest = Static<typeof MapReportRequest>;

export const MapReportReceipt = Open({ id: Uuid, status: MapReportStatus });
export type MapReportReceipt = Static<typeof MapReportReceipt>;

/** Moderation: GET /api/v1/admin/map-reports?status=open|resolved|dismissed|all&mapId=&cursor= */
export const MapReportInfo = Open({
  id: Uuid,
  map: MapInfo,
  reporter: PublicAccount,
  reason: MapReportReason,
  details: Type.String({ maxLength: 2000 }),
  status: MapReportStatus,
  createdAt: Timestamp,
  resolvedAt: Type.Optional(Timestamp),
  resolvedBy: Type.Optional(PublicAccount),
  note: Type.Optional(Type.String({ maxLength: 2000 })),
});
export type MapReportInfo = Static<typeof MapReportInfo>;

export const MapReportList = Page(MapReportInfo, 'Map reports, newest first.');
export type MapReportList = Static<typeof MapReportList>;

/** POST /api/v1/admin/map-reports/{id}/resolve (moderators) */
export const ResolveMapReportRequest = Strict({
  status: Type.Union([Type.Literal('resolved'), Type.Literal('dismissed')]),
  note: Type.Optional(Type.String({ maxLength: 2000 })),
  hideMap: Type.Optional(Type.Boolean({ description: 'Also hide the reported map.' })),
  hideReason: Type.Optional(Type.String({ minLength: 1, maxLength: 2000 })),
});
export type ResolveMapReportRequest = Static<typeof ResolveMapReportRequest>;

/** POST /api/v1/admin/maps/{id}/hide (moderators); /unhide takes no body. */
export const MapHideRequest = Strict({ reason: Type.String({ minLength: 1, maxLength: 2000 }) });
export type MapHideRequest = Static<typeof MapHideRequest>;

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
export type DeleteAccountRequest = Static<typeof DeleteAccountRequest>;
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
export type MapList = Static<typeof MapList>;
export type RatedEntity = Static<typeof RatedEntity>;
export type LeaderboardEntry = Static<typeof LeaderboardEntry>;
