// HeadlessEngineRunner against the fake binary: every job kind's success and
// failure paths, blob store I/O and the result contracts.
import { readFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { gzipSync } from 'node:zlib';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { contentKey, defaultMapPool, putContent, sha256Hex } from '@glob2/core';
import {
  engineJobs,
  schemaIssues,
  type EngineJob,
  type EngineJobKind,
  type GeneratorDescriptor,
  type MatchSetup,
} from '@glob2/protocol';
import { EngineJobError } from '../src/agent.ts';
import { EngineCrashError } from '../src/engine.ts';
import { createRunner, fakeMap, SIM, type RunnerHarness } from './support.ts';

let database: TestDatabase;
let h: RunnerHarness;
const signal = new AbortController().signal;

beforeAll(async () => {
  database = await createTestDatabase();
  h = await createRunner(database.db);
});

afterAll(async () => {
  await h?.close();
  await database?.drop();
});

function job<K extends EngineJobKind>(kind: K, payload: unknown): EngineJob {
  return { jobId: randomUUID(), kind, simVersion: SIM, payload } as EngineJob;
}

async function run(kind: EngineJobKind, payload: unknown): Promise<Record<string, unknown>> {
  const result = (await h.runner.run(job(kind, payload), signal)) as Record<string, unknown>;
  expect(schemaIssues(engineJobs[kind].result, result)).toEqual([]);
  return result;
}

async function failure(kind: EngineJobKind, payload: unknown): Promise<EngineJobError> {
  const error = await h.runner.run(job(kind, payload), signal).then(
    () => undefined,
    (e: unknown) => e,
  );
  expect(error).toBeInstanceOf(EngineJobError);
  return error as EngineJobError;
}

async function store(bytes: Uint8Array): Promise<string> {
  return (await putContent(h.store, bytes)).sha256;
}

async function readStored(sha256: string): Promise<Buffer> {
  const stream = await h.store.get(contentKey(sha256));
  const chunks: Buffer[] = [];
  for await (const chunk of stream!) chunks.push(chunk as Buffer);
  return Buffer.concat(chunks);
}

const ARENA: GeneratorDescriptor = {
  ...defaultMapPool('1v1').find((e) => e.generatorId === 'symmetric-arena')!,
  seed: 42,
};

describe('generate-map', () => {
  it('generates, stores the decompressed map by its hash and registers the blob', async () => {
    const result = await run('generate-map', { generator: ARENA });
    expect(result['map']).toEqual({ width: 128, height: 128, teamCount: 2 });
    expect(result['chosenSeed']).toBe(42 * 7 + 1);
    expect(result['startQuality']).toEqual({ fairness: 0.98, score: 0.99 });
    const bytes = await readStored(result['mapHash'] as string);
    expect(sha256Hex(bytes)).toBe(result['mapHash']);
    expect(bytes.byteLength).toBe(result['size']);
    expect(bytes.subarray(4, 4 + 'study-15-42-r0'.length).toString()).toBe('study-15-42-r0');
    const row = await database.db
      .selectFrom('blobs')
      .selectAll()
      .where('sha256', '=', result['mapHash'] as string)
      .executeTakeFirstOrThrow();
    expect(row).toMatchObject({ content_type: 'application/x-glob2-map', visibility: 'public' });
  });

  it('refuses descriptors the binary cannot honour, without running it', async () => {
    expect(
      (await failure('generate-map', { generator: { ...ARENA, revision: 9 } })).message,
    ).toMatch(/revision 1 in this engine, not 9/);
    expect(
      (await failure('generate-map', { generator: { ...ARENA, generatorId: 'atlantis' } })).code,
    ).toBe('bad_request');
    expect(
      (
        await failure('generate-map', {
          generator: { ...ARENA, params: { ...ARENA.params, lakes: 3 } },
        })
      ).message,
    ).toMatch(/no parameter lakes/);
    expect(
      (
        await failure('generate-map', {
          generator: { ...ARENA, params: { ...ARENA.params, width: 12 } },
        })
      ).message,
    ).toMatch(/width=12 is not one of 6, 7, 8, 9/);
    expect(
      (await failure('generate-map', { generator: { ...ARENA, generatorId: 'uniform' } })).message,
    ).toMatch(/not available/);
  });

  it('reports a generator refusal as a bad request', async () => {
    const error = await failure('generate-map', {
      generator: { ...ARENA, params: { ...ARENA.params, moat: 9 } },
    });
    expect(error.code).toBe('bad_request');
    expect(error.message).toMatch(/moat 9 is not buildable/);
  });
});

describe('validate-map', () => {
  it('accepts a gzip upload, storing the decompressed bytes under their own hash', async () => {
    const raw = fakeMap({ name: 'Two Rivers', minor: 118, teams: 4, width: 256, height: 128 });
    const blobHash = await store(gzipSync(raw));
    const result = await run('validate-map', { blobHash, format: 'map' });
    expect(result).toEqual({
      valid: true,
      mapHash: sha256Hex(raw),
      map: { width: 256, height: 128, teamCount: 4 },
      versionMinor: 118,
      title: 'Two Rivers',
    });
    expect(Buffer.compare(await readStored(sha256Hex(raw)), raw)).toBe(0);
  });

  it('accepts saves for save-sourced rooms and tells maps and saves apart', async () => {
    const save = fakeMap({ saved: true });
    expect(
      (await run('validate-map', { blobHash: await store(save), format: 'save' }))['valid'],
    ).toBe(true);
    expect(await run('validate-map', { blobHash: await store(save), format: 'map' })).toEqual({
      valid: false,
      reason: 'file is a saved game, not a map',
    });
    expect(
      await run('validate-map', { blobHash: await store(fakeMap({ name: 'm2' })), format: 'save' }),
    ).toEqual({ valid: false, reason: 'file is a map, not a saved game' });
  });

  it('lists the players of a save for reteaming', async () => {
    const save = fakeMap({ name: 'evening', saved: true, teams: 3 });
    const result = await run('validate-map', { blobHash: await store(save), format: 'save' });
    expect(result['players']).toEqual([
      { name: 'evening-p0', team: 0, kind: 'human' },
      { name: 'evening-p1', team: 1, kind: 'human' },
      { name: 'evening-p2', team: 2, kind: 'ai' },
    ]);
    // An engine that does not report names: slot-numbered names instead.
    const older = fakeMap({ name: 'nameless', saved: true, teams: 2 });
    expect(
      (await run('validate-map', { blobHash: await store(older), format: 'save' }))['players'],
    ).toEqual([
      { name: 'Player 1', team: 0, kind: 'human' },
      { name: 'AI 2', team: 1, kind: 'ai' },
    ]);
    // Maps never carry players.
    const map = fakeMap({ name: 'plain' });
    expect(
      (await run('validate-map', { blobHash: await store(map), format: 'map' }))['players'],
    ).toBeUndefined();
  });

  it('rejects corrupt, future, oversized and oversided files', async () => {
    const reasons = async (bytes: Uint8Array) =>
      (
        (await run('validate-map', { blobHash: await store(bytes), format: 'map' })) as {
          reason: string;
        }
      ).reason;
    expect(await reasons(Buffer.from('definitely not a map'))).toMatch(/not a Globulation 2 map/);
    expect(await reasons(Buffer.from([0x1f, 0x8b, 1, 2, 3, 4]))).toMatch(/corrupt gzip/);
    expect(await reasons(fakeMap({ minor: 126 }))).toMatch(/newer engine \(format 126/);
    expect(await reasons(fakeMap({ width: 1024, height: 1024 }))).toMatch(
      /largest accepted side is 512/,
    );
    expect(await reasons(fakeMap({ teams: 0, name: 'no teams' }))).toMatch(/0 teams/);
    // A header the engine's loader refuses (here: a truncated body).
    expect(await reasons(fakeMap({}).subarray(0, 30))).toMatch(/cannot load this map/);

    const small = await createRunner(database.db, { limits: { maxMapBytes: 200 } });
    try {
      const big = fakeMap({ name: 'x'.repeat(300) });
      const blobHash = (await putContent(small.store, big)).sha256;
      const result = (await small.runner.run(
        job('validate-map', { blobHash, format: 'map' }),
        signal,
      )) as {
        reason: string;
      };
      expect(result.reason).toMatch(/limit 200/);
      // A gzip bomb is cut off at the decompressed limit.
      const bomb = (await putContent(small.store, gzipSync(Buffer.alloc(100_000)))).sha256;
      const bombResult = (await small.runner.run(
        job('validate-map', { blobHash: bomb, format: 'map' }),
        signal,
      )) as {
        reason: string;
      };
      expect(bombResult.reason).toMatch(/exceeds 200 bytes/);
    } finally {
      await small.close();
    }
  });

  it('fails (not invalid) when the blob is missing', async () => {
    expect(
      (await failure('validate-map', { blobHash: 'ff'.repeat(32), format: 'map' })).message,
    ).toMatch(/not found/);
  });
});

describe('render-preview', () => {
  it('renders a PNG through the map loader and stores it', async () => {
    const mapHash = await store(fakeMap({ name: 'preview me' }));
    const result = await run('render-preview', { mapHash, maxSizePx: 256 });
    expect(result).toMatchObject({ contentType: 'image/png', width: 256, height: 256 });
    const png = await readStored(result['previewHash'] as string);
    expect(png.subarray(1, 4).toString()).toBe('PNG');
  });

  it('refuses sizes below the engine minimum', async () => {
    const mapHash = await store(fakeMap({ name: 'tiny' }));
    expect((await failure('render-preview', { mapHash, maxSizePx: 64 })).code).toBe('bad_request');
  });

  it('kills a hung engine at the timeout and lets the queue retry', async () => {
    const fast = await createRunner(database.db, {
      engine: {
        limits: {
          catalog: { timeoutMs: 10_000 },
          generate: { timeoutMs: 10_000 },
          inspect: { timeoutMs: 500 },
          verify: { timeoutMs: 10_000 },
        },
      },
    });
    try {
      const mapHash = (await putContent(fast.store, fakeMap({ name: 'hang' }))).sha256;
      const started = Date.now();
      const error = await fast.runner
        .run(job('render-preview', { mapHash, maxSizePx: 256 }), signal)
        .catch((e: unknown) => e);
      expect(error).toBeInstanceOf(EngineCrashError);
      expect((error as Error).message).toMatch(/timed out after 500 ms/);
      expect(Date.now() - started).toBeLessThan(5000);
    } finally {
      await fast.close();
    }
  });
});

describe('verify-match', () => {
  const setup = JSON.parse(
    readFileSync(
      new URL(
        '../../../packages/protocol/fixtures/valid/MatchSetup/catalog-1v1.json',
        import.meta.url,
      ),
      'utf8',
    ),
  ) as MatchSetup;
  let mapHash: string;

  beforeAll(async () => {
    mapHash = await store(fakeMap({ name: 'arena' }));
  });

  async function verify(record: unknown, simVersion = SIM) {
    const recordHash = await store(Buffer.from(JSON.stringify(record)));
    return {
      matchId: randomUUID(),
      setup: { ...setup, simVersion, map: { kind: 'catalog' as const, hash: mapHash } },
      recordHash,
    };
  }

  it('returns a verified outcome with team history, storing result and replay', async () => {
    const result = (await run(
      'verify-match',
      await verify({ verdict: 'verified', outcomes: ['won', 'lost'] }),
    )) as {
      verdict: string;
      outcome: {
        finalTick: number;
        teams: {
          team: number;
          outcome: string;
          eliminatedTick?: number;
          statistics: object;
          timeline: object[];
        }[];
        resultHash: string;
        replayHash: string;
      };
    };
    expect(result.verdict).toBe('verified');
    expect(result.outcome.finalTick).toBe(2100);
    expect(result.outcome.teams.map((t) => [t.team, t.outcome, t.eliminatedTick])).toEqual([
      [0, 'won', undefined],
      [1, 'lost', 2000],
    ]);
    // Real HeadlessRunner history: 5 samples for 2100 ticks, every 512.
    expect(result.outcome.teams[0]!.timeline.map((p) => (p as { tick: number }).tick)).toEqual([
      0, 512, 1024, 1536, 2048,
    ]);
    expect(result.outcome.teams[0]!.timeline[1]).toEqual({
      tick: 512,
      units: 4,
      buildings: 1,
      prestige: 0,
      hp: 1501,
      attack: 0,
      defense: 0,
    });
    expect(result.outcome.teams[0]!.statistics).toMatchObject({
      units: 7,
      workers: 5,
      totalHp: 2168,
      alive: 1,
    });
    expect((await readStored(result.outcome.replayHash)).toString()).toBe('replay of verified');
    expect(
      JSON.parse((await readStored(result.outcome.resultHash)).toString()).teams[1].outcome,
    ).toBe('lost');
    const kinds = await database.db
      .selectFrom('blobs')
      .select('content_type')
      .where('sha256', 'in', [result.outcome.replayHash, result.outcome.resultHash])
      .execute();
    expect(kinds.map((k) => k.content_type).sort()).toEqual([
      'application/json',
      'application/x-glob2-replay',
    ]);
  });

  it('maps diverged and unverifiable verdicts', async () => {
    expect(
      await run('verify-match', await verify({ verdict: 'diverged', seats: [1, 1], exit: 1 })),
    ).toMatchObject({
      verdict: 'diverged',
      clients: [1],
    });
    expect(
      await run(
        'verify-match',
        await verify({ verdict: 'unverifiable', reason: 'no seat matches tick 25' }),
      ),
    ).toEqual({ verdict: 'unverifiable', reason: 'no seat matches tick 25' });
  });

  it('retries an engine failure without a verdict and refuses another sim version', async () => {
    const error = await h.runner
      .run(job('verify-match', await verify({ noVerdict: true, exit: 3 })), signal)
      .catch((e: unknown) => e);
    expect(error).toBeInstanceOf(EngineCrashError);
    expect((await failure('verify-match', await verify({ noVerdict: true, exit: 2 }))).code).toBe(
      'bad_request',
    );
    const other = { ...SIM, dataHash: 'cd'.repeat(32) };
    expect(
      (await failure('verify-match', await verify({ verdict: 'verified' }, other))).message,
    ).toMatch(/this verifier runs/);
  });
});
