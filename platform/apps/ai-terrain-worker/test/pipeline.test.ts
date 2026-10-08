import { beforeAll, afterAll, it, expect, vi } from 'vitest';
import { randomUUID, createHash } from 'node:crypto';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import sharp from 'sharp';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { FsBlobStore } from '@glob2/core';
import { AgentBlobs } from '@glob2/engine/blobs';
import { TerrainStudio, type TerrainPlan } from '@glob2/terrain-studio';
import { newPackage, type TerrainStudioConfig } from '@glob2/protocol';
import { Pipeline, type Validator } from '../src/pipeline.ts';
import { Attempts, ProviderUncertain, type TerrainProvider } from '../src/provider.ts';
let database: TestDatabase, studio: TerrainStudio, blobs: AgentBlobs, directory: string;
const cfg: TerrainStudioConfig = {
  enabled: true,
  salesEnabled: false,
  textModel: 'test',
  imageModel: 'test',
  pipelineVersion: 'terrain-v1',
  providerCallsPerDay: 100,
  maxOutputTokens: 16000,
  timeoutSeconds: 1800,
};
const root = resolve('..'),
  python = process.env['TERRAIN_PYTHON'] ?? 'python3';
const plan: TerrainPlan = {
  action: 'build',
  text: 'Made marsh',
  brief: 'Swamp',
  title: 'Swamp',
  description: 'Wet marsh',
  entries: [
    {
      kind: 'terrain',
      operation: 'upsert',
      key: 'marsh',
      name: 'Marsh',
      preset: 'marsh',
      propertiesJson: '{}',
      yieldsJson: '{}',
      presentationJson: '{}',
      allowedResourceKeys: null,
      regenerateArt: true,
      artPrompt: 'Mossy ground',
      decorPrompt: '',
      animationFrames: 1,
    },
  ],
};
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new TerrainStudio(database.db);
  directory = await mkdtemp(resolve(tmpdir(), 'terrain-tests-'));
  blobs = new AgentBlobs(new FsBlobStore(directory), database.db);
});
afterAll(async () => {
  await database?.drop();
  if (directory) await rm(directory, { recursive: true, force: true });
});
async function fixture() {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 20) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await studio.credits.adjust(account, randomUUID(), 3, 'grant');
  const pack = newPackage('Author'),
    thread = (await studio.create(account, 'Swamp', randomUUID(), pack)).id;
  const id = randomUUID();
  await studio.submit(
    account,
    thread,
    { id, text: 'Create marsh', expectedRevision: 0, references: [] },
    cfg,
  );
  return { account, thread, id };
}
async function provider() {
  const png = await sharp({
    create: { width: 128, height: 128, channels: 4, background: '#608050' },
  })
    .png()
    .toBuffer();
  return {
    text: vi.fn<TerrainProvider['text']>(async () => ({
      text: JSON.stringify(plan),
      usage: { input_tokens: 10, output_tokens: 10 },
    })),
    image: vi.fn<TerrainProvider['image']>(async () => ({ bytes: png, usage: {} })),
  } satisfies TerrainProvider;
}
const validator: Validator = {
  validateSet: async (bytes) => ({
    report: {
      hash: createHash('sha256').update(bytes).digest('hex'),
      suite: 1,
      valid: true,
      minVersionMinor: 144,
      terrainCount: 1,
      resourceCount: 0,
    },
    png: await sharp({ create: { width: 32, height: 32, channels: 4, background: '#608050' } })
      .png()
      .toBuffer(),
  }),
};
it('delivers a validated pack through real artwork processing and settles one credit', async () => {
  const f = await fixture(),
    p = await provider();
  await new Pipeline(
    studio,
    blobs,
    p,
    validator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  ).tick();
  const t = await studio.get(f.account, f.thread);
  expect(t.requests[0]).toMatchObject({ status: 'ready', charged: true });
  expect(t.revisions[0]?.title).toBe('Swamp');
  expect(t.revisions[0]?.report.previewHash).toMatch(/^[0-9a-f]{64}$/);
  expect(t.brief).toBe('Swamp');
  const revision = await database.db
    .selectFrom('terrain_studio_revisions')
    .select('document')
    .where('request_id', '=', f.id)
    .executeTakeFirstOrThrow();
  expect((revision.document as ReturnType<typeof newPackage>).assets.sheets).toHaveLength(1);
  expect(p.image).toHaveBeenCalledTimes(1);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 2, reserved: 0 });
});
it('keeps a question conversational without image generation or a build reservation', async () => {
  const f = await fixture(),
    p = await provider();
  p.text.mockResolvedValue({
    text: JSON.stringify({
      ...plan,
      action: 'discuss',
      entries: [],
      text: 'Would you prefer wet or dry soil?',
    }),
    usage: { input_tokens: 1, output_tokens: 1 },
  });
  await new Pipeline(
    studio,
    blobs,
    p,
    validator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  ).tick();
  expect(p.image).not.toHaveBeenCalled();
  expect((await studio.credits.balance(f.account)).balance).toBe(3);
  expect((await studio.get(f.account, f.thread)).revisions).toHaveLength(0);
});
it('retains uncertain image outcomes without automatic duplicate dispatch', async () => {
  const f = await fixture(),
    p = await provider();
  p.image.mockRejectedValue(new ProviderUncertain('Unknown image result'));
  const pipeline = new Pipeline(
    studio,
    blobs,
    p,
    validator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  );
  await pipeline.tick();
  await pipeline.tick();
  expect(p.image).toHaveBeenCalledTimes(1);
  expect((await studio.request(f.id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  await studio.finish((await studio.request(f.id))!, undefined, 'Operator reconciled failure');
});
it('reuses completed provider stages after a worker restart', async () => {
  const f = await fixture(),
    row = (await studio.claim())!,
    attempts = new Attempts(studio, 100),
    call = vi.fn(async () => ({ text: JSON.stringify(plan), usage: {} }));
  await attempts.run(
    row,
    'design',
    'test',
    { brief: row.input.brief, submission: row.input.submission },
    call,
  );
  await sql`UPDATE terrain_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  const p = await provider();
  await new Pipeline(
    studio,
    blobs,
    p,
    validator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  ).tick();
  expect(p.text).not.toHaveBeenCalled();
  expect((await studio.request(f.id))?.charged).toBe(true);
});

it('repairs definitions within the original scope without buying the same image again', async () => {
  const f = await fixture(),
    p = await provider();
  let validations = 0;
  const repairValidator: Validator = {
    validateSet: async (bytes) => {
      const result = await validator.validateSet(bytes);
      return {
        ...result,
        report: { ...result.report, valid: ++validations > 1, reason: 'Fix definition' },
      };
    },
  };
  await new Pipeline(
    studio,
    blobs,
    p,
    repairValidator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  ).tick();
  expect(p.image).toHaveBeenCalledTimes(1);
  expect(p.text).toHaveBeenCalledTimes(2);
  expect(validations).toBe(2);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 2, reserved: 0 });
});

it('pauses at the current daily capacity without an extra dispatch and allows cancellation', async () => {
  await sql`DELETE FROM terrain_studio_provider_usage`.execute(database.db);
  const f = await fixture(),
    p = await provider();
  await new Pipeline(
    studio,
    blobs,
    p,
    validator,
    { ...cfg, providerCallsPerDay: 1 },
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  ).tick();
  expect(p.text).toHaveBeenCalledTimes(1);
  expect(p.image).not.toHaveBeenCalled();
  expect((await studio.progress(f.account, f.thread, f.id)).notes.at(-1)?.text).toContain('Paused');
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  await studio.cancel(f.account, f.thread, f.id);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 3, reserved: 0 });
});
it.runIf(!!process.env['TERRAIN_NATIVE_BINARY'])(
  'delivers through the actual native importer and gallery renderer',
  async () => {
    const { GlobEngine, DEFAULT_LIMITS } = await import('@glob2/engine/engine');
    const f = await fixture(),
      p = await provider();
    const engine = new GlobEngine({
      binary: resolve(process.env['TERRAIN_NATIVE_BINARY'] ?? ''),
      workdir: root,
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 32 * 1024 * 1024,
    });
    await new Pipeline(
      studio,
      blobs,
      p,
      engine,
      cfg,
      root,
      python,
      '144-1-' + 'a'.repeat(64),
    ).tick();
    const request = await studio.request(f.id);
    expect(request?.status, request?.error ?? '').toBe('ready');
    const progress = await studio.progress(f.account, f.thread, f.id);
    expect(progress.checks).toContainEqual(
      expect.objectContaining({ id: 'engine', status: 'pass' }),
    );
    const revision = (await studio.get(f.account, f.thread)).revisions[0];
    expect(revision?.report.valid).toBe(true);
    if (!revision?.report.previewHash) throw Error('Preview missing');
    const preview = await blobs.read(revision.report.previewHash, 32 * 1024 * 1024);
    expect((await sharp(preview).metadata()).width).toBe(768);
  },
);
it('uses the previous custom artwork as a reference when revising its appearance', async () => {
  const f = await fixture(),
    p = await provider();
  const pipeline = new Pipeline(
    studio,
    blobs,
    p,
    validator,
    cfg,
    root,
    python,
    '144-1-' + 'a'.repeat(64),
  );
  await pipeline.tick();
  const draft = await database.db
    .selectFrom('set_drafts')
    .select(['document', 'revision'])
    .where('id', '=', (await studio.get(f.account, f.thread)).draftId)
    .executeTakeFirstOrThrow();
  const id = randomUUID();
  await studio.submit(
    f.account,
    f.thread,
    { id, text: 'Repaint the marsh', expectedRevision: draft.revision, references: [] },
    cfg,
  );
  await pipeline.tick();
  expect((await studio.request(id))?.status).toBe('ready');
  expect(p.image).toHaveBeenCalledTimes(2);
  const original = draft.document.assets.sheets[0];
  if (!original) throw Error('Original sheet missing');
  expect(p.image.mock.calls[1]?.[2]?.[0]).toEqual(Buffer.from(original.png, 'base64url'));
});
