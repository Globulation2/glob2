import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, describe, it, expect, vi } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { StudioStore, sourceHash, type ProviderResult } from '../src/ai-studio/store.ts';
import { StudioRunner, starter } from '../src/ai-studio/runner.ts';
import { Credits } from '@glob2/billing';
let database: TestDatabase, store: StudioStore, account: string;
const rate = { version: 'test/1', model: 'test-model', input: 10, cachedInput: 1, output: 20 };
beforeAll(async () => {
  database = await createTestDatabase();
  store = new StudioStore(database.db);
  account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Studio author' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
});
afterAll(async () => {
  await database?.drop();
});
const project = () => store.create(account, 'Colony', starter);
const command = (revision = 1) => ({
  id: randomUUID(),
  expectedRevision: revision,
  text: 'Make an edit',
  budget: 20,
});
async function state(id: string) {
  return (
    await sql<{
      status: string;
      error: string | null;
    }>`SELECT status,error FROM ai_studio_requests WHERE id=${id}`.execute(database.db)
  ).rows[0]!;
}
describe('AI Studio revisions and paid requests', () => {
  it('does not recreate private content after account deletion', async () => {
    const deleted = await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Deleted studio author', status: 'deleted' })
      .returning('id')
      .executeTakeFirstOrThrow();
    await expect(store.create(deleted.id, 'New draft', starter)).rejects.toThrow(
      'No active account',
    );
  });
  it('rejects invalid source bytes and keeps profiles byte-for-byte', async () => {
    for (const source of ['', '\0', 'x'.repeat(131073), '\ud800'])
      expect(() => sourceHash(source)).toThrow();
    const source = '// profile one\nfunction step() {}\n';
    const p = await store.create(account, 'Import', source);
    expect((await store.revision(p.id, 1)).source).toBe(source);
  });
  it('owns projects, conflicts stale writers, restores immutably, and preserves checkpoints', async () => {
    const p = await project();
    await expect(store.project(p.id, randomUUID())).rejects.toThrow('No such');
    const results = await Promise.allSettled([
      store.save(p.id, account, 1, { source: starter + '\n// A' }),
      store.save(p.id, account, 1, { source: starter + '\n// B' }),
    ]);
    expect(results.filter((r) => r.status === 'fulfilled')).toHaveLength(1);
    expect((await store.project(p.id, account)).revision).toBe(2);
    await store.save(p.id, account, 2, { restoreRevision: 1 });
    expect((await store.revision(p.id, 3)).source).toBe(starter);
    expect((await store.revision(p.id, 1)).reason).toBe('initial');
  });
  it('deduplicates identical submissions and locks manual editing while work is active', async () => {
    const p = await project(),
      c = command();
    await Promise.all([store.command(p.id, account, c), store.command(p.id, account, c)]);
    await expect(store.command(p.id, account, { ...c, text: 'different' })).rejects.toThrow(
      'identifier',
    );
    await expect(store.command(p.id, account, command())).rejects.toThrow('Wait');
    await expect(store.save(p.id, account, 1, { source: starter + ' ' })).rejects.toThrow('Wait');
    await sql`UPDATE ai_studio_requests SET status='cancelled' WHERE id=${c.id}`.execute(
      database.db,
    );
  });
  it('applies complete edits once, meters usage once, releases reservations and isolates wallets', async () => {
    const p = await project(),
      c = command(),
      credits = new Credits(database.db, 'aiStudio');
    await credits.adjust(account, randomUUID(), 100, 'grant');
    const result: ProviderResult = {
      text: 'Updated staffing.',
      source: starter + '\n// adjusted',
      usage: { input: 100, cachedInput: 0, output: 100 },
    };
    const generate = vi.fn(async () => result);
    const runner = new StudioRunner(store, { generate }, rate, 2048, () => {});
    await store.command(p.id, account, c);
    await runner.sweep();
    await runner.sweep();
    await store.command(p.id, account, c);
    await runner.sweep();
    expect(generate).toHaveBeenCalledTimes(1);
    expect((await store.project(p.id, account)).revision).toBe(2);
    expect((await state(c.id)).status).toBe('completed');
    expect(await credits.balance(account)).toEqual({ balance: 99, reserved: 0, available: 99 });
    expect((await new Credits(database.db).balance(account)).balance).toBe(0);
  });
  it('discussion creates no revision; invalid edits are still metered and do not replace source', async () => {
    for (const source of [undefined, '\0']) {
      const p = await project(),
        c = command();
      const runner = new StudioRunner(
        store,
        {
          generate: async () => ({
            text: 'Explanation',
            ...(source === undefined ? {} : { source }),
            usage: { input: 10, cachedInput: 0, output: 10 },
          }),
        },
        rate,
        2048,
        () => {},
      );
      await store.command(p.id, account, c);
      await runner.sweep();
      expect((await store.project(p.id, account)).revision).toBe(1);
      expect((await state(c.id)).status).toBe(source === undefined ? 'completed' : 'failed');
      expect(
        (
          await sql<{
            charged: number;
          }>`SELECT charged FROM ai_studio_calls WHERE id=${c.id}`.execute(database.db)
        ).rows[0]!.charged,
      ).toBe(1);
    }
  });
  it('rejects insufficient request caps without dispatch or reservation', async () => {
    const p = await project(),
      c = { ...command(), budget: 1 },
      generate = vi.fn();
    const runner = new StudioRunner(store, { generate }, rate, 2048, () => {});
    await store.command(p.id, account, c);
    await runner.sweep();
    expect(generate).not.toHaveBeenCalled();
    expect((await state(c.id)).status).toBe('failed');
    expect(
      (await sql`SELECT id FROM ai_studio_calls WHERE id=${c.id}`.execute(database.db)).rows,
    ).toHaveLength(0);
  });
  it('does not redispatch an uncertain request and preserves reserved credits for reconciliation', async () => {
    const p = await project(),
      c = command(),
      generate = vi.fn(async () => {
        throw Error('Connection lost');
      }),
      runner = new StudioRunner(store, { generate }, rate, 2048, () => {});
    await store.command(p.id, account, c);
    await runner.sweep();
    await runner.sweep();
    expect(generate).toHaveBeenCalledTimes(1);
    expect((await state(c.id)).status).toBe('uncertain');
    expect((await new Credits(database.db, 'aiStudio').balance(account)).reserved).toBeGreaterThan(
      0,
    );
  });
  it('recovers a journaled result after a crash without another model call', async () => {
    const p = await project(),
      c = command(),
      credits = new Credits(database.db, 'aiStudio'),
      result = {
        text: 'Recovered edit',
        source: starter + '\n// recovered',
        usage: { input: 10, cachedInput: 0, output: 10 },
      };
    await store.command(p.id, account, c);
    await credits.reserve(account, c.id, 5, rate);
    await credits.dispatch(c.id);
    await sql`UPDATE ai_studio_requests SET status='running',lease_until=now()-interval '1 minute',provider_result=${JSON.stringify(result)}::jsonb WHERE id=${c.id}`.execute(
      database.db,
    );
    const generate = vi.fn(),
      runner = new StudioRunner(store, { generate }, rate, 2048, () => {});
    await runner.sweep();
    expect(generate).not.toHaveBeenCalled();
    expect((await store.project(p.id, account)).revision).toBe(2);
    expect((await state(c.id)).status).toBe('completed');
  });
  it('never applies an edit cancelled during generation, but settles known usage', async () => {
    const p = await project(),
      c = command();
    const runner = new StudioRunner(
      store,
      {
        generate: async () => {
          await sql`UPDATE ai_studio_requests SET cancelled=true WHERE id=${c.id}`.execute(
            database.db,
          );
          return {
            text: 'Late edit',
            source: starter + '\n// late',
            usage: { input: 1, cachedInput: 0, output: 1 },
          };
        },
      },
      rate,
      2048,
      () => {},
    );
    await store.command(p.id, account, c);
    await runner.sweep();
    expect((await store.project(p.id, account)).revision).toBe(1);
    expect((await state(c.id)).status).toBe('cancelled');
  });
});

it('serializes project creation at the account quota', async () => {
  const owner = await database.db
    .insertInto('accounts')
    .values({
      kind: 'registered',
      display_name: 'Studio quota',
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await database.db
    .insertInto('ai_studio_projects')
    .values(Array.from({ length: 99 }, (_, n) => ({ account_id: owner.id, title: `Draft ${n}` })))
    .execute();
  const results = await Promise.allSettled([
    store.create(owner.id, 'Last draft', starter),
    store.create(owner.id, 'Overflow draft', starter),
  ]);
  expect(results.filter((r) => r.status === 'fulfilled')).toHaveLength(1);
  const failure = results.find((r) => r.status === 'rejected') as PromiseRejectedResult;
  expect(failure.reason.message).toContain('100 projects');
});

it.each(['settlement', 'revision write'])(
  'recovers a journaled edit after a transient %s failure without charging or generating twice',
  async (failure) => {
    const p = await project(),
      c = command();
    const generate = vi.fn(async () => ({
      text: 'Durable edit',
      source: starter + '\n// durable',
      usage: { input: 10, cachedInput: 0, output: 10 },
    }));
    const runner = new StudioRunner(store, { generate }, rate, 2048, () => {});
    const before = (await runner.credits.balance(account)).balance;
    const injected =
      failure === 'settlement'
        ? vi.spyOn(runner.credits, 'settle').mockRejectedValueOnce(Error('Database unavailable'))
        : vi.spyOn(store, 'append').mockRejectedValueOnce(Error('Database unavailable'));
    await store.command(p.id, account, c);
    await runner.sweep();
    injected.mockRestore();
    expect((await state(c.id)).status).toBe('running');
    expect((await store.project(p.id, account)).revision).toBe(1);
    await expect(store.save(p.id, account, 1, { source: starter + ' ' })).rejects.toThrow('Wait');
    await sql`UPDATE ai_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${c.id}`.execute(
      database.db,
    );
    await runner.sweep();
    expect(generate).toHaveBeenCalledTimes(1);
    expect((await state(c.id)).status).toBe('completed');
    expect((await store.project(p.id, account)).revision).toBe(2);
    expect((await runner.credits.balance(account)).balance).toBe(before - 1);
    expect(
      (await sql`SELECT * FROM ai_studio_ledger WHERE id=${`usage:${c.id}`}`.execute(database.db))
        .rows,
    ).toHaveLength(1);
    await runner.terminal(
      { id: c.id, project_id: p.id } as Parameters<typeof runner.terminal>[0],
      'uncertain',
      'Stale failure',
    );
    expect((await state(c.id)).status).toBe('completed');
  },
);

it('retains late provider evidence after losing the lease without applying or settling it', async () => {
  const p = await project(),
    c = command();
  const runner = new StudioRunner(
    store,
    {
      generate: async () => {
        // Simulate another replica recovering an expired dispatch before the
        // original provider connection finally returns its complete response.
        await runner.credits.uncertain(c.id);
        await sql`UPDATE ai_studio_requests SET status='uncertain',lease_until=NULL WHERE id=${c.id}`.execute(
          database.db,
        );
        return {
          text: 'Late response',
          source: starter + '\n// late response',
          responseId: 'provider-late',
          usage: { input: 10, cachedInput: 0, output: 10 },
        };
      },
    },
    rate,
    2048,
    () => {},
  );
  await store.command(p.id, account, c);
  await runner.sweep();
  expect((await state(c.id)).status).toBe('uncertain');
  expect((await store.project(p.id, account)).revision).toBe(1);
  const request = await database.db
    .selectFrom('ai_studio_requests')
    .select('provider_result')
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect((request.provider_result as { responseId: string }).responseId).toBe('provider-late');
  const call = await database.db
    .selectFrom('ai_studio_calls')
    .select(['status', 'charged'])
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect(call).toEqual({ status: 'uncertain', charged: null });
});

it('settles known usage when provider text contains invalid Unicode or NUL', async () => {
  const p = await project(),
    c = command();
  const runner = new StudioRunner(
    store,
    {
      generate: async (_system, _prompt, _output, _signal, progress) => {
        await progress('Reply\0 with \ud800');
        return { text: 'Reply\0 with \ud800', usage: { input: 10, cachedInput: 0, output: 10 } };
      },
    },
    rate,
    2048,
    () => {},
  );
  await store.command(p.id, account, c);
  await runner.sweep();
  expect((await state(c.id)).status).toBe('completed');
  const request = await database.db
    .selectFrom('ai_studio_requests')
    .select(['response', 'provider_result'])
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect(request.response).toBe('Reply with \ufffd');
  expect(request.provider_result).not.toBeNull();
  const call = await database.db
    .selectFrom('ai_studio_calls')
    .select(['status', 'charged'])
    .where('id', '=', c.id)
    .executeTakeFirstOrThrow();
  expect(call).toEqual({ status: 'settled', charged: 1 });
});
