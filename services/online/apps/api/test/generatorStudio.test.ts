import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, expect, it, vi } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { Credits } from '@glob2/billing';
import {
  decodeGeneratorDraft,
  encodeGeneratorDraft,
  generatorPackage,
  importGeneratorPackage,
  importGeneratorStudioFile,
} from '@glob2/protocol';
import { StudioStore, type ProviderResult } from '../src/coding-studio/store.ts';
import { StudioRunner } from '../src/coding-studio/runner.ts';
import {
  draftHash,
  generatorStarter,
  generatorSystemPrompt,
  generatorEdit,
} from '../src/generator-studio/domain.ts';
let database: TestDatabase, store: StudioStore, account: string;
const rate = { version: 'test/1', model: 'test', input: 10, cachedInput: 1, output: 20 };
beforeAll(async () => {
  database = await createTestDatabase();
  store = new StudioStore(database.db, 'generatorStudio', draftHash);
  account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Generator author' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
});
afterAll(async () => {
  await database?.drop();
});
const command = () => ({ id: randomUUID(), expectedRevision: 1, text: 'Add a river', budget: 100 });
it('imports exactly one module, preserving the entry, and keeps invalid manifest text recoverable', () => {
  const starter = generatorStarter(),
    d = decodeGeneratorDraft(starter),
    m = JSON.parse(d.manifest);
  m.entry = 'maps/my-map.js';
  const bytes = JSON.stringify({ formatVersion: 1, manifest: m, modules: { [m.entry]: d.script } });
  expect(JSON.parse(generatorPackage(importGeneratorPackage(bytes)))).toEqual(JSON.parse(bytes));
  expect(() =>
    importGeneratorPackage(
      JSON.stringify({
        formatVersion: 1,
        manifest: m,
        modules: { [m.entry]: d.script, 'extra.js': 'export const x=1' },
      }),
    ),
  ).toThrow('no files were discarded');
  expect(() => draftHash(encodeGeneratorDraft({ manifest: '{', script: '' }))).not.toThrow();
  expect(() => generatorPackage(encodeGeneratorDraft({ manifest: '{', script: '' }))).toThrow();
  const recovery = encodeGeneratorDraft({ manifest: '{', script: d.script });
  expect(importGeneratorStudioFile(recovery)).toBe(recovery);
  expect(importGeneratorStudioFile(bytes)).toBe(importGeneratorPackage(bytes));
  expect(() => draftHash(encodeGeneratorDraft({ manifest: '{}', script: '\0' }))).toThrow();
  expect(() => generatorEdit.encode({ manifest: '{}' })).toThrow();
  expect(() => generatorEdit.encode({ manifest: '{}', script: 'x', extra: 1 })).toThrow();
});
it('gives starters fresh identities, keeps both files atomic, rejects stale edits, and restores immutably', async () => {
  const source = generatorStarter(),
    p = await store.create(account, 'Landscape', source);
  expect(JSON.parse(decodeGeneratorDraft(source).manifest).id).not.toBe(
    JSON.parse(decodeGeneratorDraft(generatorStarter()).manifest).id,
  );
  await expect(store.project(p.id, randomUUID())).rejects.toThrow('No such');
  const changed = encodeGeneratorDraft({ ...decodeGeneratorDraft(source), manifest: '{' });
  await store.save(p.id, account, 1, { source: changed });
  await expect(store.save(p.id, account, 1, { source })).rejects.toThrow('changed');
  await store.save(p.id, account, 2, { restoreRevision: 1 });
  expect((await store.revision(p.id, 3)).source).toBe(source);
  expect((await store.revision(p.id, 2)).source).toBe(changed);
});
it('settles one atomic model delivery once and keeps the generator balance separate', async () => {
  const source = generatorStarter(),
    p = await store.create(account, 'Landscape', source),
    c = command();
  const credits = new Credits(database.db, 'generatorStudio');
  await credits.adjust(account, 'generator-test', 500, 'grant');
  const updated = encodeGeneratorDraft({
    ...decodeGeneratorDraft(source),
    script: decodeGeneratorDraft(source).script + '\n// updated',
  });
  const result: ProviderResult = {
    text: 'Updated',
    source: updated,
    usage: { input: 10, cachedInput: 0, output: 10 },
  };
  const generate = vi.fn(async () => result),
    runner = new StudioRunner(store, { generate }, rate, 2048, () => {}, generatorSystemPrompt);
  await store.command(p.id, account, c);
  await store.command(p.id, account, c);
  await runner.sweep();
  await runner.sweep();
  expect(generate).toHaveBeenCalledTimes(1);
  expect((await store.revision(p.id, 2)).source).toBe(updated);
  expect(await credits.balance(account)).toEqual({ balance: 499, reserved: 0, available: 499 });
  expect((await new Credits(database.db, 'aiStudio').balance(account)).balance).toBe(0);
});
it('meters invalid edits without applying either file', async () => {
  const p = await store.create(account, 'Landscape', generatorStarter()),
    c = command();
  const credits = new Credits(database.db, 'generatorStudio');
  await credits.adjust(account, 'generator-invalid', 500, 'grant');
  const result: ProviderResult = {
    text: 'Failed edit',
    source: '{"manifest":"{}"}',
    usage: { input: 10, cachedInput: 0, output: 10 },
  };
  await store.command(p.id, account, c);
  await new StudioRunner(
    store,
    { generate: async () => result },
    rate,
    2048,
    () => {},
    generatorSystemPrompt,
  ).sweep();
  expect((await store.project(p.id, account)).revision).toBe(1);
  expect(
    (
      await sql<{
        status: string;
      }>`SELECT status FROM generator_studio_requests WHERE id=${c.id}`.execute(database.db)
    ).rows[0]?.status,
  ).toBe('failed');
  expect((await credits.balance(account)).reserved).toBe(0);
});
it('never redispatches an unknown provider outcome', async () => {
  const p = await store.create(account, 'Landscape', generatorStarter()),
    c = command();
  const credits = new Credits(database.db, 'generatorStudio');
  await credits.adjust(account, 'generator-uncertain', 500, 'grant');
  const generate = vi.fn(async () => {
      throw Error('lost result');
    }),
    runner = new StudioRunner(store, { generate }, rate, 2048, () => {}, generatorSystemPrompt);
  await store.command(p.id, account, c);
  await runner.sweep();
  await runner.sweep();
  expect(generate).toHaveBeenCalledTimes(1);
  expect((await credits.balance(account)).reserved).toBeGreaterThan(0);
  expect((await store.project(p.id, account)).revision).toBe(1);
});
it('recovers a journaled result without another model call', async () => {
  const source = generatorStarter(),
    p = await store.create(account, 'Landscape', source),
    c = command(),
    credits = new Credits(database.db, 'generatorStudio');
  await credits.adjust(account, 'generator-journal', 500, 'grant');
  await store.command(p.id, account, c);
  await credits.reserve(account, c.id, 10, rate);
  await credits.dispatch(c.id);
  const updated = encodeGeneratorDraft({
    ...decodeGeneratorDraft(source),
    script: 'export function generate(c) {}',
  });
  const result: ProviderResult = {
    text: 'Recovered',
    source: updated,
    usage: { input: 10, cachedInput: 0, output: 10 },
  };
  await sql`UPDATE generator_studio_requests SET status='running',lease_until=now()-interval '1 minute',provider_result=${JSON.stringify(result)}::jsonb WHERE id=${c.id}`.execute(
    database.db,
  );
  const generate = vi.fn();
  await new StudioRunner(store, { generate }, rate, 2048, () => {}, generatorSystemPrompt).sweep();
  expect(generate).not.toHaveBeenCalled();
  expect((await store.revision(p.id, 2)).source).toBe(updated);
  expect((await credits.balance(account)).reserved).toBeGreaterThanOrEqual(0);
});
it('cancels generated delivery without changing either file and still settles known usage', async () => {
  const source = generatorStarter(),
    p = await store.create(account, 'Cancel', source),
    c = command();
  const credits = new Credits(database.db, 'generatorStudio');
  await credits.adjust(account, 'generator-cancel-test', 500, 'grant');
  const reservedBefore = (await credits.balance(account)).reserved;
  const runner = new StudioRunner(
    store,
    {
      async generate() {
        await sql`UPDATE generator_studio_requests SET cancelled=true WHERE id=${c.id}`.execute(
          database.db,
        );
        return {
          text: 'Late delivery',
          source: encodeGeneratorDraft({ ...decodeGeneratorDraft(source), script: '// cancelled' }),
          usage: { input: 10, cachedInput: 0, output: 10 },
        };
      },
    },
    rate,
    2048,
    () => {},
    generatorSystemPrompt,
  );
  await store.command(p.id, account, c);
  await runner.sweep();
  expect((await store.project(p.id, account)).revision).toBe(1);
  expect((await store.revision(p.id, 1)).source).toBe(source);
  const row = await database.db
    .selectFrom('generator_studio_requests')
    .select('status')
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect(row.status).toBe('cancelled');
  expect((await credits.balance(account)).reserved).toBe(reservedBefore);
  const call = await database.db
    .selectFrom('generator_studio_calls')
    .select('charged')
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect(Number(call.charged)).toBe(1);
});
