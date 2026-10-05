import { afterAll, beforeAll, expect, it } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { directoryQuery } from '../src/history/players.ts';
let database: TestDatabase;
beforeAll(async () => {
  database = await createTestDatabase();
  await sql`INSERT INTO accounts (kind,display_name) SELECT 'registered','Player ' || lpad(n::text,6,'0') FROM generate_series(1,12000) n`.execute(
    database.db,
  );
  await sql`VACUUM ANALYZE accounts`.execute(database.as('admin').db);
});
afterAll(async () => database?.drop());
it('uses the directory indexes for alphabetic browsing and selective substring searches', async () => {
  const browse = directoryQuery('', { participants: 'all', limit: 24 }, [
    { id: 'nicowar', name: 'Nicowar' },
  ]);
  const search = directoryQuery('004321', { participants: 'all', limit: 8 }, []);
  const explain = async (query: typeof browse) =>
    JSON.stringify((await sql`EXPLAIN (FORMAT JSON) ${query}`.execute(database.db)).rows);
  expect(await explain(browse)).toContain('accounts_directory_name');
  expect(await explain(search)).toContain('accounts_directory_search');
  const result = await browse.execute(database.db);
  expect(result.rows).toHaveLength(25);
  expect((await search.execute(database.db)).rows.map((r) => r.name)).toEqual(['Player 004321']);
});

it('seeks directly to late alphabetical pages without rescanning earlier results', async () => {
  const query = directoryQuery('', { participants: 'humans', limit: 24 }, [], {
    relevance: 0,
    name: 'player 011900',
    kind: 'ai',
    id: 'nicowar',
  });
  const plan = JSON.stringify(
    (await sql`EXPLAIN (ANALYZE, FORMAT JSON) ${query}`.execute(database.db)).rows,
  );
  expect(plan).toMatch(/accounts_(directory_name|registered_display_name_key)/);
  expect(plan).toContain('Index Cond');
  const result = await query.execute(database.db);
  expect(result.rows).toHaveLength(25);
  expect(result.rows[0]?.name).toBe('Player 011901');
});
