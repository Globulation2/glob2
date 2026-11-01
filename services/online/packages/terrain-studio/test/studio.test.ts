import { beforeAll, afterAll, expect, it } from 'vitest';
import { randomUUID, createHash } from 'node:crypto';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { newPackage, type TerrainStudioConfig } from '@glob2/protocol';
import { TerrainStudio } from '../src/studio.ts';
let database: TestDatabase, studio: TerrainStudio;
const cfg: TerrainStudioConfig = {
  enabled: true,
  salesEnabled: false,
  textModel: 'test',
  imageModel: 'test',
  pipelineVersion: 'terrain-v1',
  providerCallsPerDay: 100,
};
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new TerrainStudio(database.db);
});
afterAll(async () => {
  await database?.drop();
});
async function fixture() {
  const a = await database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: randomUUID().slice(0, 20) })
    .returning('id')
    .executeTakeFirstOrThrow();
  await studio.credits.adjust(a.id, randomUUID(), 3, 'grant');
  const pack = newPackage('Author'),
    thread = (await studio.create(a.id, 'Swamp', randomUUID(), pack)).id;
  const input = { id: randomUUID(), text: 'Make marsh', expectedRevision: 0, references: [] };
  await studio.submit(a.id, thread, input, cfg);
  const row = (await studio.claim())!;
  return { account: a.id, thread, row, input, pack };
}
async function delivery(pack: ReturnType<typeof newPackage>) {
  const hash = createHash('sha256').update(JSON.stringify(pack)).digest('hex');
  await database.db
    .insertInto('blobs')
    .values({
      sha256: hash,
      size: JSON.stringify(pack).length,
      content_type: 'application/json',
      storage_key: 'test/' + hash,
      visibility: 'private',
    })
    .onConflict((c) => c.column('sha256').doNothing())
    .execute();
  return {
    package: pack,
    hash,
    report: {
      hash,
      suite: 1 as const,
      valid: true,
      minVersionMinor: 144,
      terrainCount: 0,
      resourceCount: 0,
    },
    simVersion: '144-1-' + 'a'.repeat(64),
    text: 'Delivered',
  };
}
it('deduplicates submissions and settles a validated private delivery exactly once', async () => {
  const f = await fixture();
  expect(await studio.submit(f.account, f.thread, f.input, cfg)).toEqual({ id: f.input.id });
  await studio.reserveBuild(f.row);
  await studio.reserveBuild(f.row);
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  const d = await delivery(f.pack);
  await studio.finish(f.row, d);
  await studio.finish(f.row, d);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 2, reserved: 0 });
  const t = await studio.get(f.account, f.thread);
  expect(t.revisions[0]?.applied).toBe(true);
  const draft = await database.db
    .selectFrom('set_drafts')
    .selectAll()
    .where('id', '=', f.pack.versionId)
    .executeTakeFirstOrThrow();
  expect(draft.status).toBe('valid');
  expect(draft.revision).toBe(1);
  await expect(studio.get(randomUUID(), f.thread)).rejects.toThrow('No such');
});
it('retains a concurrent-edit candidate and charges delivery once; adoption requires the current revision', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  await sql`UPDATE set_drafts SET revision=1,document=jsonb_set(document,'{description}','"Manual changes"') WHERE id=${f.pack.versionId}`.execute(
    database.db,
  );
  await studio.finish(f.row, await delivery(f.pack));
  expect((await studio.get(f.account, f.thread)).revisions[0]?.applied).toBe(false);
  await expect(studio.adopt(f.account, f.thread, f.row.id, 0)).rejects.toThrow('changed');
  await studio.adopt(f.account, f.thread, f.row.id, 1);
  expect((await studio.credits.balance(f.account)).balance).toBe(2);
});
it('keeps discussion free and returns failed build reservations', async () => {
  const f = await fixture();
  await studio.finish(f.row, { text: 'Tell me more.', brief: 'Swamp' });
  expect((await studio.credits.balance(f.account)).balance).toBe(3);
  await studio.submit(f.account, f.thread, { ...f.input, id: randomUUID() }, cfg);
  const next = (await studio.claim())!;
  await studio.reserveBuild(next);
  await studio.finish(next, undefined, 'Invalid artwork');
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 3, reserved: 0 });
});
it('fences cancellation and retains uncertain provider reservations', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  await studio.checkpoint(f.row, 'dispatched', {});
  await expect(studio.cancel(f.account, f.thread, f.row.id)).rejects.toThrow('provider outcome');
  await studio.checkpoint(f.row, 'uncertain', {});
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  await studio.finish(f.row, undefined, 'Reconciled failure');
  expect((await studio.credits.balance(f.account)).reserved).toBe(0);
});
it('rejects reused IDs with altered content and stale base revisions', async () => {
  const f = await fixture();
  await expect(
    studio.submit(f.account, f.thread, { ...f.input, text: 'Different' }, cfg),
  ).rejects.toThrow('identifier');
  await studio.finish(f.row, { text: 'Done' });
  await expect(
    studio.submit(f.account, f.thread, { ...f.input, id: randomUUID(), expectedRevision: 7 }, cfg),
  ).rejects.toThrow('changed');
});
it('retains saved drafts through generation and revision-safe undo without changing charges', async () => {
  const f = await fixture();
  const before = await database.db
    .selectFrom('set_drafts')
    .selectAll()
    .where('id', '=', f.pack.versionId)
    .executeTakeFirstOrThrow();
  const changed = {
    ...f.pack,
    title: 'Generated marsh',
    description: 'Generated description',
    tags: ['marsh'],
  };
  await studio.reserveBuild(f.row);
  await studio.finish(f.row, await delivery(changed));
  expect((await studio.get(f.account, f.thread)).draftHistory?.map((d) => d.revision)).toContain(
    before.revision,
  );
  expect(await studio.draftBackup(f.account, f.thread, before.revision)).toEqual(before.document);
  await expect(studio.draftBackup(randomUUID(), f.thread, before.revision)).rejects.toThrow();
  await expect(
    studio.restoreDraft(f.account, f.thread, before.revision, before.revision),
  ).rejects.toThrow('Draft changed');
  const balance = await studio.credits.balance(f.account);
  await studio.restoreDraft(f.account, f.thread, before.revision, before.revision + 1);
  const after = await database.db
    .selectFrom('set_drafts')
    .selectAll()
    .where('id', '=', f.pack.versionId)
    .executeTakeFirstOrThrow();
  expect(after.document).toEqual(before.document);
  const set = await database.db
    .selectFrom('asset_sets')
    .select(['title', 'description', 'tags'])
    .where('id', '=', f.pack.setId)
    .executeTakeFirstOrThrow();
  expect(set).toEqual({ title: f.pack.title, description: f.pack.description, tags: f.pack.tags });
  expect(after.revision).toBe(before.revision + 2);
  expect((await studio.get(f.account, f.thread)).draftHistory?.map((d) => d.revision)).toContain(
    before.revision + 1,
  );
  expect(await studio.credits.balance(f.account)).toEqual(balance);
});
