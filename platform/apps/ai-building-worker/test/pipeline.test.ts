import { beforeAll, afterAll, it, expect, vi } from 'vitest';
import { randomUUID } from 'node:crypto';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import sharp from 'sharp';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { FsBlobStore } from '@glob2/core';
import { AgentBlobs } from '@glob2/engine/blobs';
import {
  BuildingAiStudio,
  newBuildingPackage,
  type BuildingPlan,
  plannerPrompt,
} from '@glob2/building-studio';
import { writeBuildingArchive } from '@glob2/protocol/node';
import type { BuildingAiStudioConfig } from '@glob2/protocol';
import { Pipeline, type Validator } from '../src/pipeline.ts';
import {
  Attempts,
  OpenAIBuildings,
  ProviderUncertain,
  type BuildingProvider,
} from '../src/provider.ts';
let database: TestDatabase, studio: BuildingAiStudio, blobs: AgentBlobs, directory: string;
const root = resolve('..'),
  cfg: BuildingAiStudioConfig = {
    enabled: true,
    salesEnabled: false,
    textModel: 'test',
    imageModel: 'test',
    pipelineVersion: 'building-v1',
    providerCallsPerDay: 100,
    maxOutputTokens: 16000,
    timeoutSeconds: 1800,
  };
const entry = {
  key: 'building',
  operation: 'upsert' as const,
  next: null,
  previous: null,
  requiredExperiment: null,
  propertiesJson: '{"maxUnitInside":4,"insideSpeed":64}',
  semanticsJson: '{"healing":{"enabled":true,"unitMask":7,"duration":20,"cost":{}}}',
  presentationJson: '{"displayName":"Mushroom hospital"}',
  regenerateArt: true,
  teamColor: false,
  artPrompt: 'Mushroom hospital',
};
const plan: BuildingPlan = {
  action: 'build',
  scope: 'both',
  text: 'Made a hospital',
  brief: 'Hospital',
  title: 'Hospital',
  experimentsJson: null,
  entries: [entry],
};
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new BuildingAiStudio(database.db);
  directory = await mkdtemp(resolve(tmpdir(), 'building-tests-'));
  blobs = new AgentBlobs(new FsBlobStore(directory), database.db);
});
afterAll(async () => {
  await database?.drop();
  if (directory) await rm(directory, { recursive: true, force: true });
});
async function fixture(config = cfg, references: string[] = []) {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 20) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await studio.credits.adjust(account, randomUUID(), 3, 'grant');
  const pack = newBuildingPackage(randomUUID()),
    archive = writeBuildingArchive(pack, new Map());
  const thread = (await studio.create(account, 'Hospital', randomUUID(), archive)).id,
    t = await studio.own(account, thread);
  for (const hash of references)
    await sql`INSERT INTO building_studio_artifacts(thread_id,stage,kind,label,hash) VALUES(${thread},'prepare','reference','Test reference',${hash})`.execute(
      database.db,
    );
  const draft = await database.db
    .selectFrom('building_drafts')
    .select('revision')
    .where('id', '=', t.draftId)
    .executeTakeFirstOrThrow();
  const id = randomUUID();
  await studio.submit(
    account,
    thread,
    { id, text: 'Create a mushroom hospital', expectedRevision: draft.revision, references },
    config,
    await blobs.write(archive, 'application/zip'),
  );
  return { account, thread, id };
}
async function provider() {
  const shape = await sharp({
    create: { width: 80, height: 100, channels: 4, background: '#906f41' },
  })
    .png()
    .toBuffer();
  const png = await sharp({
    create: { width: 128, height: 128, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 0 } },
  })
    .composite([{ input: shape, left: 24, top: 14 }])
    .png()
    .toBuffer();
  return {
    text: vi.fn<BuildingProvider['text']>(async () => ({ text: JSON.stringify(plan), usage: {} })),
    image: vi.fn<BuildingProvider['image']>(async () => ({ bytes: png, usage: {} })),
  } satisfies BuildingProvider;
}
const validator: Validator = {
  composeBuildings: async () => ({
    schemaVersion: 1,
    baseHash: 'a'.repeat(64),
    catalog: { snapshot: '{}', hash: 'b'.repeat(64) },
  }),
};
function pipeline(p: BuildingProvider, v = validator) {
  return new Pipeline(studio, blobs, p, v, cfg, root, 'test');
}
it('delivers normalized artwork in a validated immutable archive and charges one credit', async () => {
  const f = await fixture(),
    p = await provider();
  await pipeline(p).tick();
  const t = await studio.get(f.account, f.thread);
  expect(t.requests[0]).toMatchObject({ status: 'ready', charged: true });
  expect(t.revisions[0]?.report.valid).toBe(true);
  expect(t.revisions[0]?.package.sprites).toHaveLength(2);
  expect(p.image).toHaveBeenCalledTimes(1);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 2, reserved: 0 });
});
it('answers questions without images or a credit charge', async () => {
  const f = await fixture(),
    p = await provider();
  p.text.mockResolvedValue({
    text: JSON.stringify({ ...plan, action: 'discuss', entries: [] }),
    usage: {},
  });
  await pipeline(p).tick();
  expect(p.image).not.toHaveBeenCalled();
  expect((await studio.credits.balance(f.account)).balance).toBe(3);
});
it('keeps stock camera references when all four player reference slots are used', async () => {
  const hashes = await Promise.all(
    [0, 1, 2, 3].map(async (n) => {
      const png = await sharp({
        create: {
          width: 4,
          height: 4,
          channels: 4,
          background: { r: 40 + n, g: 50, b: 60, alpha: 1 },
        },
      })
        .png()
        .toBuffer();
      return blobs.write(png, 'image/png');
    }),
  );
  const f = await fixture(cfg, hashes),
    p = await provider();
  await pipeline(p).tick();
  expect((await studio.request(f.id))?.status).toBe('ready');
  expect(p.image.mock.calls[0]?.[2]).toHaveLength(7);
});
it('creates the finished structure first and reuses its source for construction identity', async () => {
  const f = await fixture(),
    p = await provider();
  p.text.mockResolvedValue({
    text: JSON.stringify({
      ...plan,
      entries: [
        {
          ...entry,
          key: 'site',
          next: 'building',
          previous: '',
          propertiesJson: '{"isBuildingSite":1,"width":2,"height":2}',
          semanticsJson: '{"placeable":true,"constructionCost":{"wood":3}}',
        },
        {
          ...entry,
          previous: 'site',
          semanticsJson: '{"placeable":false,"instantPlacement":false}',
        },
      ],
    }),
    usage: {},
  });
  await pipeline(p).tick();
  expect((await studio.request(f.id))?.status).toBe('ready');
  const first = await p.image.mock.results[0]!.value;
  expect(p.image.mock.calls[1]?.[2][0]).toEqual(first.bytes);
  expect(p.image.mock.calls[1]?.[2]).toHaveLength(4);
});
it('repairs once without purchasing the same image twice', async () => {
  const f = await fixture(),
    p = await provider();
  let n = 0;
  await pipeline(p, {
    composeBuildings: async (...args) => {
      if (++n === 1) throw Error('Invalid definition');
      return validator.composeBuildings(...args);
    },
  }).tick();
  expect(p.text).toHaveBeenCalledTimes(2);
  expect(p.image).toHaveBeenCalledTimes(1);
  expect((await studio.request(f.id))?.charged).toBe(true);
});
it('retains unknown provider outcomes without duplicate dispatch', async () => {
  const f = await fixture(),
    p = await provider();
  p.image.mockRejectedValue(new ProviderUncertain('Unknown result'));
  const worker = pipeline(p);
  await worker.tick();
  await worker.tick();
  expect(p.image).toHaveBeenCalledTimes(1);
  expect((await studio.request(f.id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  await studio.finish((await studio.request(f.id))!, undefined, 'Reconciled');
});
it('resumes a completed design stage after a restart', async () => {
  const f = await fixture(),
    row = (await studio.claim())!,
    p = await provider();
  const reference = await readFile(resolve(root, 'docs/features/building-catalogs.md'), 'utf8'),
    examples = await Promise.all(
      ['inn', 'hospital', 'defencetower', 'swarm'].map((name) =>
        readFile(resolve(root, 'data/buildings', name + '.json'), 'utf8'),
      ),
    );
  const prompt = plannerPrompt(
    row.input.base,
    row.input.messages,
    row.input.brief,
    reference + '\nStock definitions: ' + examples.join('\n'),
  );
  await new Attempts(studio, 100).run(row, 'design', 'test', { prompt }, async () => ({
    text: JSON.stringify(plan),
    usage: {},
  }));
  await sql`UPDATE building_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  await pipeline(p).tick();
  expect(p.text).not.toHaveBeenCalled();
  expect((await studio.request(f.id))?.charged).toBe(true);
});
it('rejects a repair that expands gameplay during an appearance-only request', async () => {
  const f = await fixture(),
    p = await provider();
  const appearance = {
    ...plan,
    scope: 'appearance',
    entries: [{ ...entry, propertiesJson: '{}', semanticsJson: '{}' }],
  };
  p.text
    .mockResolvedValueOnce({ text: JSON.stringify(appearance), usage: {} })
    .mockResolvedValueOnce({ text: JSON.stringify(plan), usage: {} });
  await pipeline(p, {
    composeBuildings: async () => {
      throw Error('Reject');
    },
  }).tick();
  expect((await studio.request(f.id))?.error).toContain('scope');
  expect((await studio.credits.balance(f.account)).balance).toBe(3);
});
it.runIf(!!process.env['BUILDING_NATIVE_BINARY'])(
  'validates through the actual native decoder and retains archive evidence',
  async () => {
    const { GlobEngine, DEFAULT_LIMITS } = await import('@glob2/engine/engine');
    const f = await fixture(),
      p = await provider();
    const realImage = process.env['BUILDING_VALIDATION_IMAGE'];
    if (realImage) {
      const finished = await readFile(resolve(realImage));
      const site = await readFile(
        resolve(process.env['BUILDING_VALIDATION_SITE_IMAGE'] ?? realImage),
      );
      p.image
        .mockResolvedValueOnce({ bytes: finished, usage: {} })
        .mockResolvedValueOnce({ bytes: site, usage: {} });
    }
    p.text.mockResolvedValue({
      text: JSON.stringify({
        ...plan,
        entries: [
          {
            ...entry,
            previous: 'site',
            teamColor: !!realImage,
            semanticsJson:
              '{"placeable":false,"instantPlacement":false,"healing":{"enabled":true,"unitMask":7,"duration":20,"cost":{}}}',
          },
          {
            ...entry,
            key: 'site',
            teamColor: !!realImage,
            next: 'building',
            previous: '',
            propertiesJson: '{"width":2,"height":2,"isBuildingSite":1,"hpInit":1,"hpMax":200}',
            semanticsJson: '{"placeable":true,"constructionCost":{"wood":3}}',
            presentationJson: '{"displayName":"Mushroom hospital construction"}',
          },
        ],
      }),
      usage: {},
    });
    const engine = new GlobEngine({
      binary: resolve(process.env['BUILDING_NATIVE_BINARY']!),
      workdir: root,
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 32 * 1024 * 1024,
    });
    await pipeline(p, engine).tick();
    const t = await studio.get(f.account, f.thread);
    expect(t.requests[0]?.error).toBeNull();
    expect(t.requests[0]?.status).toBe('ready');
    const revision = await database.db
      .selectFrom('building_studio_revisions')
      .select(['hash', 'report'])
      .where('request_id', '=', f.id)
      .executeTakeFirstOrThrow();
    const evidence = resolve(root, 'artifacts/building-studio');
    await mkdir(evidence, { recursive: true });
    await writeFile(
      resolve(evidence, 'hospital.zip'),
      await blobs.read(revision.hash, 32 * 1024 * 1024),
    );
    await writeFile(
      resolve(evidence, 'hospital-report.json'),
      JSON.stringify(revision.report, null, 2),
    );
  },
);
it('revises properties without dispatching image generation', async () => {
  const f = await fixture(),
    p = await provider();
  p.text.mockResolvedValue({
    text: JSON.stringify({
      ...plan,
      scope: 'properties',
      entries: [{ ...entry, regenerateArt: false }],
    }),
    usage: {},
  });
  await pipeline(p).tick();
  expect(p.image).not.toHaveBeenCalled();
  expect((await studio.request(f.id))?.status).toBe('ready');
  expect((await studio.credits.balance(f.account)).balance).toBe(2);
});
it.runIf(
  !!process.env['BUILDING_OPENAI_API_KEY'] &&
    !!process.env['BUILDING_LIVE_TEXT_MODEL'] &&
    !!process.env['BUILDING_LIVE_IMAGE_MODEL'] &&
    !!process.env['BUILDING_NATIVE_BINARY'],
)(
  'generates a real building family through the live paid provider and native engine',
  async () => {
    const { GlobEngine, DEFAULT_LIMITS } = await import('@glob2/engine/engine');
    const liveConfig = {
      ...cfg,
      textModel: process.env['BUILDING_LIVE_TEXT_MODEL']!,
      imageModel: process.env['BUILDING_LIVE_IMAGE_MODEL']!,
    };
    const f = await fixture(liveConfig);
    const engine = new GlobEngine({
      binary: resolve(process.env['BUILDING_NATIVE_BINARY']!),
      workdir: root,
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 32 * 1024 * 1024,
    });
    await new Pipeline(
      studio,
      blobs,
      new OpenAIBuildings(process.env['BUILDING_OPENAI_API_KEY']!),
      engine,
      liveConfig,
      root,
      'test',
    ).tick();
    const row = await studio.request(f.id);
    expect(row?.error).toBeNull();
    expect(row?.status).toBe('ready');
    const revision = await database.db
      .selectFrom('building_studio_revisions')
      .select(['hash', 'report', 'document'])
      .where('request_id', '=', f.id)
      .executeTakeFirstOrThrow();
    const evidence = resolve(root, 'artifacts/building-studio/live');
    await mkdir(evidence, { recursive: true });
    const archive = await blobs.read(revision.hash, 32 * 1024 * 1024);
    await writeFile(resolve(evidence, 'hospital.zip'), archive);
    await writeFile(resolve(evidence, 'report.json'), JSON.stringify(revision.report, null, 2));
    const { readBuildingArchive } = await import('@glob2/protocol/node');
    const decoded = readBuildingArchive(Buffer.from(archive));
    expect(decoded.package.variants.some((v) => v.properties['isBuildingSite'] === 1)).toBe(true);
    expect(decoded.package.variants.some((v) => v.semantics['healing'])).toBe(true);
    expect((await studio.credits.balance(f.account)).balance).toBe(2);
    for (const variant of decoded.package.variants) {
      const sprite = decoded.package.sprites.find(
        (s) => 'package:' + s.key === variant.properties['gameSprite'],
      );
      const frame = sprite?.frames[0];
      expect(frame).toBeDefined();
      const base = decoded.assets.get(frame!.imageHash)!;
      const team = frame!.teamColorHash ? decoded.assets.get(frame!.teamColorHash) : undefined;
      const preview = team ? sharp(base).composite([{ input: team }]) : sharp(base);
      await writeFile(resolve(evidence, variant.key + '.png'), await preview.png().toBuffer());
    }
  },
  30 * 60 * 1000,
);
it('allows free discussion with no available building credits', async () => {
  const f = await fixture(),
    p = await provider();
  await studio.finish((await studio.claim())!, { text: 'First reply' });
  await studio.credits.adjust(f.account, randomUUID(), -3, 'adjustment');
  const t = await studio.own(f.account, f.thread),
    draft = await database.db
      .selectFrom('building_drafts')
      .select(['revision', 'archive'])
      .where('id', '=', t.draftId)
      .executeTakeFirstOrThrow();
  await studio.submit(
    f.account,
    f.thread,
    {
      id: randomUUID(),
      text: 'What can buildings do?',
      expectedRevision: draft.revision,
      references: [],
    },
    cfg,
    await blobs.write(draft.archive, 'application/zip'),
  );
  p.text.mockResolvedValue({
    text: JSON.stringify({ ...plan, action: 'discuss', entries: [] }),
    usage: {},
  });
  await pipeline(p).tick();
  expect(p.image).not.toHaveBeenCalled();
  expect((await studio.credits.balance(f.account)).balance).toBe(0);
});
