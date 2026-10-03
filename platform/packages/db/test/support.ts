// Test databases: each test file gets a fresh database on the server named by
// TEST_DATABASE_URL (a role allowed to CREATE DATABASE), migrated to latest,
// and dropped afterwards.
//
// Locally: docker run -d -p 127.0.0.1:55432:5432 -e POSTGRES_USER=glob2 \
//   -e POSTGRES_PASSWORD=glob2 postgres:16
import { randomBytes } from 'node:crypto';
import pg from 'pg';
import { createDatabase, migrateToLatest, type DatabaseHandle } from '../src/index.ts';

export const TEST_DATABASE_URL =
  process.env['TEST_DATABASE_URL'] ?? 'postgres://glob2:glob2@127.0.0.1:55432/postgres';

export interface TestDatabase extends DatabaseHandle {
  url: string;
  drop(): Promise<void>;
}

export async function createTestDatabase(
  options: { migrate?: boolean } = {},
): Promise<TestDatabase> {
  const name = `glob2_test_${randomBytes(6).toString('hex')}`;
  const admin = new pg.Client({ connectionString: TEST_DATABASE_URL });
  await admin.connect();
  try {
    await admin.query(`CREATE DATABASE ${name}`);
  } finally {
    await admin.end();
  }
  const url = new URL(TEST_DATABASE_URL);
  url.pathname = `/${name}`;
  const handle = createDatabase({ connectionString: url.toString(), maxConnections: 5 });
  if (options.migrate !== false) await migrateToLatest(handle.db);
  return {
    ...handle,
    url: url.toString(),
    async drop() {
      await handle.close();
      const client = new pg.Client({ connectionString: TEST_DATABASE_URL });
      await client.connect();
      try {
        await client.query(`DROP DATABASE IF EXISTS ${name} WITH (FORCE)`);
      } finally {
        await client.end();
      }
    },
  };
}
