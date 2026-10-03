// Engine-agent job contracts. Anything that needs the engine runs as a job for
// one simulation version: an engine-agent built from that version's glob2
// binary runs only the task identifiers of its own sim version.
//
// Transport: the platform records an EngineJob in the engine_jobs table; an
// agent of that sim version leases it over platform-api's internal HTTP API
// (below), runs it and reports its result, which platform-api enqueues as an
// EngineJobResult under ENGINE_RESULT_TASK (graphile-worker) for apps/worker
// to apply. Blobs (maps, saves, records, previews, replays) move through the
// same HTTP API by SHA-256. engineTaskIdentifier names the job in logs.
import { Type, type Static, type TSchema } from 'typebox';
import { ErrorBody, Open, SeatIndex, Sha256Hex, Strict, TeamIndex, Uuid } from './common.ts';
import { GeneratorDescriptor, MatchSetup } from './matchSetup.ts';
import { SimVersion, simVersionKey } from './simVersion.ts';

export const ENGINE_JOB_KINDS = [
  'generate-map',
  'validate-map',
  'render-preview',
  'verify-match',
] as const;
export type EngineJobKind = (typeof ENGINE_JOB_KINDS)[number];

/** Task the engine-agent enqueues with its EngineJobResult. */
export const ENGINE_RESULT_TASK = 'platform:engine-job-result';

/** graphile-worker task identifier for a job kind on one sim version. */
export function engineTaskIdentifier(kind: EngineJobKind, simVersion: SimVersion): string {
  return `engine:${kind}:${simVersionKey(simVersion)}`;
}

const MapFacts = Open({
  width: Type.Integer({ minimum: 1 }),
  height: Type.Integer({ minimum: 1 }),
  teamCount: Type.Integer({ minimum: 1, maximum: 12 }),
});

// ------------------------------------------------------------ generate-map

export const GenerateMapPayload = Strict({
  generator: GeneratorDescriptor,
});
export const GenerateMapResult = Open({
  mapHash: Sha256Hex,
  size: Type.Integer({ minimum: 0 }),
  map: MapFacts,
  chosenSeed: Type.Integer({ minimum: 0, maximum: 4294967295 }),
  startQuality: Type.Optional(
    Open(
      { fairness: Type.Number(), score: Type.Number() },
      { description: 'Lobby start quality.' },
    ),
  ),
});

// ------------------------------------------------------------ validate-map

export const ValidateMapPayload = Strict({
  blobHash: Sha256Hex,
  format: Type.Union([Type.Literal('map'), Type.Literal('save')]),
});
export const ValidateMapResult = Type.Union([
  Open({
    valid: Type.Literal(true),
    mapHash: Sha256Hex,
    map: MapFacts,
    versionMinor: Type.Integer({
      minimum: 0,
      description: 'Format version the file was saved with.',
    }),
    title: Type.Optional(Type.String({ maxLength: 128 })),
    players: Type.Optional(
      Type.Array(
        Open({
          name: Type.String({ maxLength: 64 }),
          team: TeamIndex,
          kind: Type.Union([Type.Literal('human'), Type.Literal('ai')]),
        }),
        {
          maxItems: 12,
          description: 'Saves only: the players recorded in the file, for reteaming.',
        },
      ),
    ),
  }),
  Open({ valid: Type.Literal(false), reason: Type.String({ maxLength: 2000 }) }),
]);

// ---------------------------------------------------------- render-preview

export const RenderPreviewPayload = Strict({
  mapHash: Sha256Hex,
  maxSizePx: Type.Integer({ minimum: 16, maximum: 2048 }),
});
export const RenderPreviewResult = Open({
  previewHash: Sha256Hex,
  contentType: Type.Literal('image/png'),
  width: Type.Integer({ minimum: 1 }),
  height: Type.Integer({ minimum: 1 }),
});

// ------------------------------------------------------------ verify-match

export const VerifyMatchPayload = Strict({
  matchId: Uuid,
  setup: MatchSetup,
  recordHash: Sha256Hex,
});

/** Maximum timeline samples per team: 4096 × 512 ticks ≈ 23 hours of play. */
export const MAX_TIMELINE_SAMPLES = 4096;

export const TeamTimelinePoint = Open(
  {
    tick: Type.Integer({ minimum: 0 }),
    units: Type.Integer(),
    buildings: Type.Integer(),
    prestige: Type.Integer(),
    hp: Type.Integer(),
    attack: Type.Integer(),
    defense: Type.Integer(),
  },
  { description: "One sample of the engine's end-of-game statistics, taken every 512 ticks." },
);

export const VerifiedTeam = Open({
  team: TeamIndex,
  outcome: Type.Union([Type.Literal('won'), Type.Literal('lost'), Type.Literal('unresolved')]),
  eliminatedTick: Type.Optional(Type.Integer({ minimum: 0 })),
  prestige: Type.Integer(),
  statistics: Type.Optional(
    Type.Record(Type.String({ pattern: '^[A-Za-z][A-Za-z0-9]{0,63}$' }), Type.Number(), {
      maxProperties: 64,
      description:
        'Final team counters from the verifier result (units, workers, buildings, totalHp, food, …).',
    }),
  ),
  timeline: Type.Optional(
    Type.Array(TeamTimelinePoint, {
      maxItems: MAX_TIMELINE_SAMPLES,
      description: 'Team history from the verifier result, oldest first.',
    }),
  ),
});

export const VerifiedOutcome = Open(
  {
    finalTick: Type.Integer({ minimum: 0 }),
    teams: Type.Array(VerifiedTeam),
    resultHash: Sha256Hex,
    replayHash: Sha256Hex,
  },
  { description: 'Outcome replayed by the engine; result.json and .replay are stored as blobs.' },
);

export const VerifyVerdict = Type.Union(
  [
    Open({ verdict: Type.Literal('verified'), outcome: VerifiedOutcome }),
    Open({
      verdict: Type.Literal('diverged'),
      clients: Type.Array(SeatIndex, {
        minItems: 1,
        uniqueItems: true,
        description: 'Seats whose reported checksums disagree with the verifier.',
      }),
      outcome: VerifiedOutcome,
    }),
    Open({
      verdict: Type.Literal('unverifiable'),
      reason: Type.String({
        maxLength: 2000,
        description: 'No client matched the verifier: engine nondeterminism or a corrupt record.',
      }),
    }),
  ],
  { description: 'verify-match result.' },
);
export type VerifyVerdict = Static<typeof VerifyVerdict>;

// ------------------------------------------------------------- envelopes

interface JobContract {
  payload: TSchema;
  result: TSchema;
}

export const engineJobs = {
  'generate-map': { payload: GenerateMapPayload, result: GenerateMapResult },
  'validate-map': { payload: ValidateMapPayload, result: ValidateMapResult },
  'render-preview': { payload: RenderPreviewPayload, result: RenderPreviewResult },
  'verify-match': { payload: VerifyMatchPayload, result: VerifyVerdict },
} as const satisfies Record<EngineJobKind, JobContract>;

export type EngineJobPayload<K extends EngineJobKind> = Static<(typeof engineJobs)[K]['payload']>;
export type EngineJobOutput<K extends EngineJobKind> = Static<(typeof engineJobs)[K]['result']>;

const JobKind = Type.Union([
  Type.Literal('generate-map'),
  Type.Literal('validate-map'),
  Type.Literal('render-preview'),
  Type.Literal('verify-match'),
]);

export const EngineJob = Type.Union(
  ENGINE_JOB_KINDS.map((kind) =>
    Strict({
      jobId: Uuid,
      kind: Type.Literal(kind),
      simVersion: SimVersion,
      payload: engineJobs[kind].payload,
    }),
  ),
  { description: 'A job for an engine agent of one sim version.' },
);
export type EngineJob = {
  [K in EngineJobKind]: {
    jobId: string;
    kind: K;
    simVersion: SimVersion;
    payload: EngineJobPayload<K>;
  };
}[EngineJobKind];

export const EngineJobResult = Type.Union(
  [
    Open({
      jobId: Uuid,
      kind: JobKind,
      ok: Type.Literal(true),
      result: Type.Object({}),
      agent: Type.String({ maxLength: 128 }),
    }),
    Open({
      jobId: Uuid,
      kind: JobKind,
      ok: Type.Literal(false),
      error: ErrorBody,
      agent: Type.String({ maxLength: 128 }),
    }),
  ],
  { description: 'What an engine agent reports for a job; result is checked against the kind.' },
);
export type EngineJobResult = { jobId: string; kind: EngineJobKind; agent: string } & (
  { ok: true; result: unknown } | { ok: false; error: Static<typeof ErrorBody> }
);

export type GenerateMapPayload = Static<typeof GenerateMapPayload>;
export type GenerateMapResult = Static<typeof GenerateMapResult>;
export type ValidateMapPayload = Static<typeof ValidateMapPayload>;
export type ValidateMapResult = Static<typeof ValidateMapResult>;
export type RenderPreviewPayload = Static<typeof RenderPreviewPayload>;
export type RenderPreviewResult = Static<typeof RenderPreviewResult>;
export type VerifyMatchPayload = Static<typeof VerifyMatchPayload>;
export type VerifiedTeam = Static<typeof VerifiedTeam>;
export type TeamTimelinePoint = Static<typeof TeamTimelinePoint>;
export type VerifiedOutcome = Static<typeof VerifiedOutcome>;

// ------------------------------------------------- engine-agent HTTP API
//
// Engine agents hold no database or blob-store credentials: they call
// platform-api's /internal/v1/engine endpoints with a bearer agent key
// (docs/multiplayer/architecture.md, "Engine agents"):
//
//   POST /internal/v1/engine/agents/heartbeat      EngineAgentHeartbeat
//   DELETE /internal/v1/engine/agents/{agentId}
//   POST /internal/v1/engine/jobs/lease            EngineLeaseRequest -> EngineLease | 204
//   POST /internal/v1/engine/jobs/{jobId}/extend   EngineLeaseExtendRequest
//   POST /internal/v1/engine/jobs/{jobId}/release  EngineLeaseReleaseRequest
//   POST /internal/v1/engine/jobs/{jobId}/result   EngineJobReport
//   GET  /internal/v1/engine/blobs/{sha256}        bytes of a blob the leased job names
//   PUT  /internal/v1/engine/blobs?contentType=&visibility=   bytes -> EngineBlobReceipt
//
// Calls about a job carry its lease token in ENGINE_LEASE_HEADER.

/** Header carrying the lease token on job and blob calls. */
export const ENGINE_LEASE_HEADER = 'x-glob2-lease';

export const EngineAgentId = Type.String({ pattern: '^[A-Za-z0-9._-]{1,128}$' });
const LeaseSeconds = Type.Integer({ minimum: 10, maximum: 3600 });
const JobKinds = Type.Array(JobKind, { minItems: 1, maxItems: 4, uniqueItems: true });

export const EngineAgentHeartbeat = Strict({
  agentId: EngineAgentId,
  simVersion: SimVersion,
  kinds: JobKinds,
  build: Type.String({ minLength: 1, maxLength: 128 }),
});

export const EngineLeaseRequest = Strict({
  agentId: EngineAgentId,
  simVersion: SimVersion,
  kinds: JobKinds,
  leaseSeconds: LeaseSeconds,
});

export const EngineLease = Strict({
  job: EngineJob,
  leaseToken: Type.String({ minLength: 32, maxLength: 128 }),
  attempt: Type.Integer({ minimum: 1 }),
  maxAttempts: Type.Integer({ minimum: 1 }),
  leaseExpiresAt: Type.String(),
});

export const EngineLeaseExtendRequest = Strict({ leaseSeconds: LeaseSeconds });

export const EngineLeaseReleaseRequest = Strict({
  retryAfterSeconds: Type.Integer({ minimum: 0, maximum: 3600 }),
});

export const EngineJobReport = Type.Union([
  Strict({ ok: Type.Literal(true), result: Type.Object({}) }),
  Strict({ ok: Type.Literal(false), error: ErrorBody }),
]);

export const EngineBlobReceipt = Strict({
  sha256: Sha256Hex,
  size: Type.Integer({ minimum: 0 }),
});

export type EngineAgentHeartbeat = Static<typeof EngineAgentHeartbeat>;
export type EngineLeaseRequest = Static<typeof EngineLeaseRequest>;
export type EngineLease = Omit<Static<typeof EngineLease>, 'job'> & { job: EngineJob };
export type EngineJobReport =
  { ok: true; result: unknown } | { ok: false; error: Static<typeof ErrorBody> };
export type EngineBlobReceipt = Static<typeof EngineBlobReceipt>;
