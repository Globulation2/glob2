// Match history, profiles and leaderboards (M5, plan section G): the read-only
// REST shapes behind the web app's leaderboard, player and match pages and
// the game's profile screen. Everything here is server-emitted, so objects are
// Open (unknown fields allowed).
//
//   GET /api/v1/leaderboards/{ladder}?provisional=include|exclude&cursor=&limit=  LeaderboardPage
//   GET /api/v1/leaderboards/{ladder}/ai                                           AiLeaderboard
//   GET /api/v1/players/{accountId}                                                PlayerProfile
//   GET /api/v1/players/{accountId}/matches?queue=&cursor=&limit=                  MatchList
//   GET /api/v1/matches?queue=&cursor=&limit=                                      MatchList (recent public)
//   GET /api/v1/matches/{id}                                                       MatchDetail
//   GET /api/v1/matches/{id}/artifacts/{record|replay|result}                      the file
//   GET /api/v1/admin/matches?q=&status=&cursor=                                   MatchList (moderators)
//   GET /api/v1/stats                                                              InstanceStats
import { Type, type Static } from 'typebox';
import { Open, SeatIndex, Timestamp, Uuid } from './common.ts';
import { MatchSetup } from './matchSetup.ts';
import { SimVersion } from './simVersion.ts';
import {
  AccountStatus,
  LeaderboardEntry,
  MatchArtifactInfo,
  MatchSummary,
  MatchTeamStats,
  PublicAccount,
} from './resources.ts';

const Count = Type.Integer({ minimum: 0 });

// ----------------------------------------------------------- leaderboards

/** AI rating entities of one ladder, grouped by sim version (AI revisions are never combined). */
export const AiLeaderboard = Open(
  {
    ladder: Type.String({ maxLength: 64 }),
    groups: Type.Array(
      Open({
        simVersion: SimVersion,
        current: Type.Boolean({
          description: 'An engine agent of this instance currently serves this sim version.',
        }),
        entries: Type.Array(LeaderboardEntry),
      }),
      { description: 'Newest sim version first.' },
    ),
  },
  { description: 'GET /api/v1/leaderboards/{ladder}/ai.' },
);
export type AiLeaderboard = Static<typeof AiLeaderboard>;

// ---------------------------------------------------------------- profiles

export const PlayerRating = Open({
  ladder: Type.String({ maxLength: 64 }),
  rating: Type.Number({ description: 'Displayed rating: ordinal scaled around 1500.' }),
  mu: Type.Number(),
  sigma: Type.Number(),
  games: Count,
  wins: Count,
  provisional: Type.Boolean(),
  rank: Type.Optional(
    Type.Integer({
      minimum: 1,
      description: 'Position among registered players of the ladder (provisional included).',
    }),
  ),
});
export type PlayerRating = Static<typeof PlayerRating>;

/** One rating change: a point of the profile's rating graph. */
export const RatingHistoryPoint = Open({
  ladder: Type.String({ maxLength: 64 }),
  matchId: Uuid,
  at: Timestamp,
  result: Type.Union([Type.Literal('won'), Type.Literal('lost')]),
  before: Type.Number(),
  after: Type.Number(),
  provisional: Type.Boolean({ description: 'Provisional after this match.' }),
});
export type RatingHistoryPoint = Static<typeof RatingHistoryPoint>;

/** Win rate over recent verified games, by queue ('room' for rooms), map hash or generator id. */
export const WinRate = Open({
  dimension: Type.Union([Type.Literal('queue'), Type.Literal('map'), Type.Literal('generator')]),
  key: Type.String({ maxLength: 128 }),
  label: Type.Optional(
    Type.String({ maxLength: 128, description: 'Queue name, catalog map title or generator id.' }),
  ),
  mapId: Type.Optional(Uuid),
  games: Count,
  wins: Count,
  winRate: Type.Number({ minimum: 0, maximum: 1 }),
});
export type WinRate = Static<typeof WinRate>;

/** A team's economy in one match next to the player's own average at the same tick. */
export const EconomyPoint = Open({
  tick: Count,
  units: Type.Integer(),
  buildings: Type.Integer(),
  prestige: Type.Integer(),
  averageUnits: Type.Number(),
  averageBuildings: Type.Number(),
  averagePrestige: Type.Number(),
  gamesAtTick: Count,
});
export type EconomyPoint = Static<typeof EconomyPoint>;

export const EconomyCurve = Open({
  matchId: Uuid,
  accountId: Uuid,
  seat: Type.Optional(SeatIndex),
  points: Type.Array(EconomyPoint),
});
export type EconomyCurve = Static<typeof EconomyCurve>;

export const PlayerAggregates = Open(
  {
    windowDays: Type.Integer({ minimum: 1 }),
    games: Count,
    wins: Count,
    losses: Count,
    winRates: Type.Array(WinRate),
    medianTicks: Type.Optional(Type.Number({ minimum: 0 })),
    meanTicks: Type.Optional(Type.Number({ minimum: 0 })),
    economy: Type.Optional(EconomyCurve),
  },
  { description: 'Aggregates over recent verified matches (the 0004 history views).' },
);
export type PlayerAggregates = Static<typeof PlayerAggregates>;

export const PlayerProfile = Open(
  {
    account: PublicAccount,
    status: Type.Optional(
      Type.Union([AccountStatus], {
        description: 'Present for moderators only; the public never sees banned accounts.',
      }),
    ),
    detail: Type.Union([Type.Literal('full'), Type.Literal('minimal')], {
      description:
        'minimal for guests: no ratings, aggregates or rating history (guests are never ranked).',
    }),
    ratings: Type.Array(PlayerRating),
    ratingHistory: Type.Array(RatingHistoryPoint, { description: 'Oldest first.' }),
    recentMatches: Type.Array(MatchSummary, { description: 'Newest first.' }),
    aggregates: Type.Optional(PlayerAggregates),
  },
  { description: 'GET /api/v1/players/{accountId}.' },
);
export type PlayerProfile = Static<typeof PlayerProfile>;

// ----------------------------------------------------------------- matches

/** Seats that sequenced orders the verifier refused (verdict.json orderRejections). */
export const OrderRejection = Open({
  seat: SeatIndex,
  rejected: Count,
  stale: Count,
  reasons: Type.Record(Type.String({ maxLength: 64 }), Count),
  firstRejectedTick: Type.Optional(Count),
});
export type OrderRejection = Static<typeof OrderRejection>;

export const VerificationDetail = Open({
  divergedSeats: Type.Optional(Type.Array(SeatIndex)),
  reason: Type.Optional(Type.String({ maxLength: 2000 })),
  orderRejections: Type.Optional(Type.Array(OrderRejection)),
  ratingNote: Type.Optional(
    Type.String({ maxLength: 500, description: 'Why ratings did or did not change.' }),
  ),
});
export type VerificationDetail = Static<typeof VerificationDetail>;

export const MatchMapInfo = Open({
  title: Type.Optional(Type.String({ maxLength: 128 })),
  mapId: Type.Optional(Uuid),
  generatorId: Type.Optional(Type.String({ maxLength: 64 })),
  width: Type.Optional(Type.Integer({ minimum: 1 })),
  height: Type.Optional(Type.Integer({ minimum: 1 })),
});
export type MatchMapInfo = Static<typeof MatchMapInfo>;

const Millis = Type.Integer({ minimum: 0 });
const Spread = Open({ p50: Millis, p95: Millis });

/**
 * One human player's connection in a match, condensed from the relay's network
 * summary (RelayNetworkSummary v1, stored per participant). Times in milliseconds.
 */
export const ParticipantNetwork = Open(
  {
    seat: SeatIndex,
    quality: Type.Union([Type.Literal('good'), Type.Literal('fair'), Type.Literal('poor')], {
      description:
        'A rough label from the numbers below (docs/development/network-telemetry.md, match page).',
    }),
    rttMs: Type.Optional(
      Type.Union([Spread], {
        description:
          'Round trip to the relay (median and 95th percentile). Absent if not measured.',
      }),
    ),
    lagMs: Type.Optional(
      Type.Union([Spread], {
        description:
          'How far the player’s game ran behind the relay clock: input delay plus any stall.',
      }),
    ),
    disconnects: Count,
    offlineMs: Type.Integer({
      minimum: 0,
      description: 'Time disconnected and inside the reconnect grace period.',
    }),
    ordersSequenced: Count,
    ordersDeferred: Type.Integer({
      minimum: 0,
      description: 'Orders placed later than requested (one order per player per tick).',
    }),
    rejoins: Type.Integer({
      minimum: 0,
      description: 'Times the relay told the game to reload after its state disagreed.',
    }),
    leftBy: Type.Optional(Type.Union([Type.Literal('quit'), Type.Literal('grace')])),
  },
  { description: 'Connection quality of one player in a match.' },
);
export type ParticipantNetwork = Static<typeof ParticipantNetwork>;

export const MatchDetail = Open(
  {
    match: MatchSummary,
    setup: MatchSetup,
    teams: Type.Array(MatchTeamStats),
    artifacts: Type.Array(MatchArtifactInfo),
    map: Type.Optional(MatchMapInfo),
    verificationDetail: Type.Optional(VerificationDetail),
    economy: Type.Optional(
      Type.Array(EconomyCurve, {
        description: "Each human player's curve in this match next to their own recent average.",
      }),
    ),
    network: Type.Optional(
      Type.Array(ParticipantNetwork, {
        description: 'Per human player, from the relay’s report; absent when the relay sent none.',
      }),
    ),
  },
  { description: 'GET /api/v1/matches/{id}.' },
);
export type MatchDetail = Static<typeof MatchDetail>;

// ------------------------------------------------------------- live stats

/** Activity numbers for the web app's home page; cached by the API for a short while. */
export const InstanceStats = Open(
  {
    playersOnline: Type.Integer({
      minimum: 0,
      description:
        'Accounts connected in the last activeWindowMinutes, in a room, or playing a match that has not ended.',
    }),
    activeWindowMinutes: Type.Integer({ minimum: 1 }),
    liveMatches: Type.Integer({ minimum: 0, description: 'Matches starting or running now.' }),
    matchesToday: Type.Integer({
      minimum: 0,
      description: 'Matches created in the last 24 hours, cancelled ones excluded.',
    }),
    generatedAt: Timestamp,
  },
  { description: 'GET /api/v1/stats.' },
);
export type InstanceStats = Static<typeof InstanceStats>;
