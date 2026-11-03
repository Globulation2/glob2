import { beforeAll, afterAll, it, expect } from 'vitest';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { newBuildingPackage } from '../src/assembly.ts';
import { BuildingAiStudio } from '../src/studio.ts';
import { writeBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import type { BuildingAiStudioConfig } from '@glob2/protocol';
let database: TestDatabase, studio: BuildingAiStudio;
const cfg: BuildingAiStudioConfig = {
  enabled: true,
  salesEnabled: false,
  textModel: 'test',
  imageModel: 'test',
  pipelineVersion: 'building-v1',
  providerCallsPerDay: 100,
};
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new BuildingAiStudio(database.db);
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
  const pack = newBuildingPackage(randomUUID()),
    archive = writeBuildingArchive(pack, new Map()),
    hash = buildingAssetHash(archive);
  await database.db
    .insertInto('blobs')
    .values({
      sha256: hash,
      size: archive.length,
      content_type: 'application/zip',
      storage_key: 'test/' + hash,
      visibility: 'private',
    })
    .onConflict((c) => c.column('sha256').doNothing())
    .execute();
  const thread = (await studio.create(a.id, 'Hospital', randomUUID(), archive)).id,
    t = await studio.own(a.id, thread);
  const draft = await database.db
    .selectFrom('building_drafts')
    .select('revision')
    .where('id', '=', t.draftId)
    .executeTakeFirstOrThrow();
  const input = {
    id: randomUUID(),
    text: 'Make a hospital',
    expectedRevision: draft.revision,
    references: [],
  };
  await studio.submit(a.id, thread, input, cfg, hash);
  const row = (await studio.claim())!;
  const delivery = {
    package: pack,
    archive,
    hash,
    title: 'Hospital',
    report: {
      valid: true as const,
      archiveHash: hash,
      baseHash: 'b'.repeat(64),
      catalog: { hash: 'c'.repeat(64), snapshot: '{}' },
      suite: 1 as const,
    },
    simVersion: 'test',
    text: 'Delivered',
  };
  return { account: a.id, thread, draftId: t.draftId, row, input, hash, delivery };
}
it('deduplicates turns and settles successful deliveries exactly once', async () => {
  const f = await fixture();
  await studio.submit(f.account, f.thread, f.input, cfg, f.hash);
  await studio.reserveBuild(f.row);
  await studio.finish(f.row, f.delivery);
  await studio.finish(f.row, f.delivery);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 2, reserved: 0 });
  const t = await studio.get(f.account, f.thread);
  expect(t.revisions).toHaveLength(1);
  expect(t.revisions[0]?.applied).toBe(true);
  const draft = await database.db
    .selectFrom('building_drafts')
    .select('revision')
    .where('id', '=', f.draftId)
    .executeTakeFirstOrThrow();
  expect(t.revisions[0]?.appliedRevision).toBe(draft.revision);
  expect(t.revisions[0]?.package.namespace).toBe(f.delivery.package.namespace);
});
it('preserves concurrent manual edits, then restores a candidate with compare-and-swap', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  const manual = randomUUID();
  await sql`UPDATE building_drafts SET revision=${manual},name='Manual' WHERE id=${f.draftId}`.execute(
    database.db,
  );
  await studio.finish(f.row, f.delivery);
  expect((await studio.get(f.account, f.thread)).revisions[0]?.applied).toBe(false);
  await expect(
    studio.adopt(f.account, f.thread, f.row.id, f.input.expectedRevision, f.delivery.archive),
  ).rejects.toThrow('changed');
  await studio.adopt(f.account, f.thread, f.row.id, manual, f.delivery.archive);
  const adopted = await database.db
    .selectFrom('building_drafts')
    .select('revision')
    .where('id', '=', f.draftId)
    .executeTakeFirstOrThrow();
  expect((await studio.get(f.account, f.thread)).revisions[0]?.appliedRevision).toBe(
    adopted.revision,
  );
  expect(adopted.revision).not.toBe(manual);
  expect((await studio.credits.balance(f.account)).balance).toBe(2);
});
it('keeps discussion free and refunds failed builds', async () => {
  const f = await fixture();
  await studio.finish(f.row, { text: 'Which unit classes should it heal?' });
  expect((await studio.credits.balance(f.account)).balance).toBe(3);
  const g = await fixture();
  await studio.reserveBuild(g.row);
  await studio.finish(g.row, undefined, 'Invalid artwork');
  expect(await studio.credits.balance(g.account)).toMatchObject({ balance: 3, reserved: 0 });
});
it('rejects mismatched verdicts and does not delete an active provider journal', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  await expect(
    studio.finish(f.row, {
      ...f.delivery,
      report: { ...f.delivery.report, archiveHash: 'd'.repeat(64) },
    }),
  ).rejects.toThrow('validated');
  await expect(studio.removeDraft(f.account, f.draftId)).rejects.toThrow('active');
  await studio.finish(f.row, undefined, 'Failed');
  await studio.removeDraft(f.account, f.draftId);
  await expect(studio.own(f.account, f.thread)).rejects.toThrow('No such');
});
it('marks expired dispatched provider calls uncertain without returning or spending the reservation', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  await sql`UPDATE building_studio_requests SET status='dispatched',lease_until=now()-interval '1 minute' WHERE id=${f.row.id}`.execute(
    database.db,
  );
  await studio.recoverUncertain();
  expect((await studio.request(f.row.id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(f.account)).reserved).toBe(1);
  await expect(studio.cancel(f.account, f.thread, f.row.id)).rejects.toThrow('provider outcome');
  await studio.finish((await studio.request(f.row.id))!, undefined, 'Reconciled');
  expect((await studio.credits.balance(f.account)).reserved).toBe(0);
});
it('fences stale workers before reusing an existing build reservation', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  const nextLease = randomUUID();
  await sql`UPDATE building_studio_requests SET lease=${nextLease} WHERE id=${f.row.id}`.execute(
    database.db,
  );
  await expect(studio.reserveBuild(f.row)).rejects.toThrow('lease expired');
  const resumed = (await studio.request(f.row.id))!;
  await studio.reserveBuild(resumed);
  expect(await studio.credits.balance(f.account)).toMatchObject({ balance: 3, reserved: 1 });
  await studio.finish(resumed, undefined, 'Finished test');
});
it('keeps every retained candidate accessible beyond the first fifty revisions', async () => {
  const f = await fixture();
  await studio.reserveBuild(f.row);
  await studio.finish(f.row, f.delivery);
  const ids = Array.from({ length: 50 }, () => randomUUID());
  await database.db
    .insertInto('building_studio_requests')
    .values(
      ids.map((id) => ({
        id,
        account_id: f.account,
        thread_id: f.thread,
        kind: 'generate' as const,
        status: 'ready' as const,
        input: JSON.stringify(f.row.input),
        charged: true,
      })),
    )
    .execute();
  await database.db
    .insertInto('building_studio_revisions')
    .values(
      ids.map((id) => ({
        request_id: id,
        thread_id: f.thread,
        base_revision: f.input.expectedRevision,
        title: 'Retained candidate',
        document: f.delivery.package,
        archive: f.delivery.archive,
        hash: f.delivery.hash,
        report: f.delivery.report,
        sim_version: 'test',
        applied: false,
      })),
    )
    .execute();
  const snapshot = await studio.get(f.account, f.thread);
  expect(snapshot.revisions).toHaveLength(51);
  expect(snapshot.revisions.some((r) => r.requestId === f.row.id)).toBe(true);
});
it('preserves replaced manual drafts, restores with a new revision, and rejects stale undo', async () => {
  const f = await fixture();
  const before = await database.db
    .selectFrom('building_drafts')
    .selectAll()
    .where('id', '=', f.draftId)
    .executeTakeFirstOrThrow();
  await studio.reserveBuild(f.row);
  await studio.finish(f.row, f.delivery);
  const generated = await database.db
    .selectFrom('building_drafts')
    .selectAll()
    .where('id', '=', f.draftId)
    .executeTakeFirstOrThrow();
  expect((await studio.get(f.account, f.thread)).draftHistory?.map((d) => d.revision)).toContain(
    before.revision,
  );
  expect((await studio.draftBackup(f.account, f.thread, before.revision)).archive).toEqual(
    before.archive,
  );
  await expect(studio.draftBackup(randomUUID(), f.thread, before.revision)).rejects.toThrow();
  await expect(
    studio.restoreDraft(f.account, f.thread, before.revision, before.revision),
  ).rejects.toThrow('Draft changed');
  const balance = await studio.credits.balance(f.account);
  await studio.restoreDraft(f.account, f.thread, before.revision, generated.revision);
  const restored = await database.db
    .selectFrom('building_drafts')
    .selectAll()
    .where('id', '=', f.draftId)
    .executeTakeFirstOrThrow();
  expect(restored.archive).toEqual(before.archive);
  expect(restored.revision).not.toBe(before.revision);
  expect(restored.revision).not.toBe(generated.revision);
  expect((await studio.get(f.account, f.thread)).draftHistory?.map((d) => d.revision)).toContain(
    generated.revision,
  );
  expect(await studio.credits.balance(f.account)).toEqual(balance);
});
