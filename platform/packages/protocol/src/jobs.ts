// Engine-agent job contracts. Anything that needs the engine runs as a job for
// one simulation version: an engine-agent built from that version's glob2
// binary runs only the task identifiers of its own sim version.
//
// Transport: graphile-worker. The platform enqueues an EngineJob under
// engineTaskIdentifier(kind, simVersion); the agent runs it and enqueues an
// EngineJobResult under ENGINE_RESULT_TASK, which apps/worker applies. Blobs
// (maps, saves, records, previews, replays) are exchanged through the shared
// blob store by SHA-256.
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

export const VerifiedTeam = Open({
  team: TeamIndex,
  outcome: Type.Union([Type.Literal('won'), Type.Literal('lost'), Type.Literal('unresolved')]),
  eliminatedTick: Type.Optional(Type.Integer({ minimum: 0 })),
  prestige: Type.Integer(),
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
export type VerifiedOutcome = Static<typeof VerifiedOutcome>;
