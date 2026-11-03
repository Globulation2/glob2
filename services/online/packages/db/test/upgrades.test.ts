// Migrations that move existing rows: each is applied to a database migrated
// to the version before it and seeded with rows in the old shape.
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createMigrator, migrateToLatest } from '../src/index.ts';
import { createTestDatabase, type TestDatabase } from './support.ts';

let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase({ migrate: false, role: 'migrator' });
});

afterAll(async () => {
  await database?.drop();
});

describe('0019_warm_maps_over_generated', () => {
  it('keeps taken pool rows and drops the rest, which the next refill replaces', async () => {
    const db = database.db;
    const { error } = await createMigrator(db).migrateTo('0017_retention');
    expect(error).toBeUndefined();
    const sim = `128-51-${'ab'.repeat(32)}`;
    const entry = 'cd'.repeat(32);
    const hash = 'ef'.repeat(32);
    for (const [status, mapHash] of [
      ['generating', null],
      ['ready', hash],
      ['failed', null],
      ['taken', hash],
    ] as const) {
      await sql`
        INSERT INTO warm_maps (queue_id, sim_version, entry_key, generator, status, map_hash, taken_at)
        VALUES ('casual-1v1', ${sim}, ${entry}, '{"generatorId":"even-ground"}', ${status},
                ${mapHash}, ${status === 'taken' ? sql`now()` : null})`.execute(db);
    }

    await migrateToLatest(db);
    const rows = await sql<{
      queue_id: string;
      entry_key: string;
      descriptor_hash: string | null;
      taken: boolean;
    }>`SELECT queue_id, entry_key, descriptor_hash, taken_at IS NOT NULL AS taken
       FROM warm_maps`.execute(db);
    expect(rows.rows).toEqual([
      { queue_id: 'casual-1v1', entry_key: entry, descriptor_hash: null, taken: true },
    ]);

    // An untaken pool row must name a generated map.
    await expect(
      sql`INSERT INTO warm_maps (queue_id, sim_version, entry_key)
          VALUES ('casual-1v1', ${sim}, ${entry})`.execute(db),
    ).rejects.toThrow(/warm_maps_pooled_check/);
  });
});
