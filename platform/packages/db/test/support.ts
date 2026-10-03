// Test databases: each test file gets a fresh database on the server named by
// TEST_DATABASE_URL (a superuser, as in the postgres image), set up like a
// deployment: least-privilege roles (src/roles.ts), migrations run as the
// migrator, and the test connected as a service role (glob2_api unless the
// test asks for another), so missing grants fail the tests. The database is
// dropped afterwards.
//
// Locally: docker run -d -p 127.0.0.1:55432:5432 -e POSTGRES_USER=glob2 \
//   -e POSTGRES_PASSWORD=glob2 postgres:16
import { randomBytes } from 'node:crypto';
import pg from 'pg';
import {
  DB_ROLES,
  connectionStringAs,
  createDatabase,
  ensureRoles,
  migrateDatabase,
  prepareDatabase,
  type DatabaseHandle,
  type DbRole,
} from '../src/index.ts';

export const TEST_DATABASE_URL =
  process.env['TEST_DATABASE_URL'] ?? 'postgres://glob2:glob2@127.0.0.1:55432/postgres';

/** Test role passwords (roles are cluster-wide; every test database shares them). */
export const TEST_ROLE_PASSWORDS: Record<DbRole, string> = {
  migrator: 'glob2-test-migrator',
  api: 'glob2-test-api',
  worker: 'glob2-test-worker',
};

export interface TestDatabase extends DatabaseHandle {
  /** Connection string of the role the handle uses. */
  url: string;
  /** Database name. */
  name: string;
  /** Connection string for another role (or the superuser, `admin`). */
  urlAs(role: DbRole | 'admin'): string;
  /** Another handle on the same database, as `role` (closed by drop()). */
  as(role: DbRole | 'admin'): DatabaseHandle;
  drop(): Promise<void>;
}

export async function createTestDatabase(
  options: {
    migrate?: boolean;
    role?: DbRole | 'admin';
    /** False: leave the database as the superuser made it (no grants, no ownership changes). */
    prepare?: boolean;
  } = {},
): Promise<TestDatabase> {
  const name = `glob2_test_${randomBytes(6).toString('hex')}`;
  const admin = new pg.Client({ connectionString: TEST_DATABASE_URL });
  await admin.connect();
  try {
    // Roles are cluster-wide: create them on the shared database, where the
    // advisory lock serialises parallel test files.
    await ensureRoles(admin, TEST_ROLE_PASSWORDS);
    await admin.query(`CREATE DATABASE ${name}`);
  } finally {
    await admin.end();
  }
  const adminUrl = new URL(TEST_DATABASE_URL);
  adminUrl.pathname = `/${name}`;
  const urlAs = (role: DbRole | 'admin') =>
    role === 'admin'
      ? adminUrl.toString()
      : connectionStringAs(adminUrl.toString(), DB_ROLES[role], TEST_ROLE_PASSWORDS[role]);

  if (options.prepare !== false) {
    const setup = new pg.Client({ connectionString: adminUrl.toString() });
    await setup.connect();
    try {
      await prepareDatabase(setup);
    } finally {
      await setup.end();
    }
  }
  if (options.migrate !== false && options.prepare !== false) {
    const migrator = createDatabase({ connectionString: urlAs('migrator'), maxConnections: 2 });
    try {
      await migrateDatabase(migrator);
    } finally {
      await migrator.close();
    }
  }

  const role = options.role ?? 'api';
  const url = urlAs(role);
  const handle = createDatabase({ connectionString: url, maxConnections: 5 });
  const others: DatabaseHandle[] = [];
  return {
    ...handle,
    url,
    name,
    urlAs,
    as(other) {
      const extra = createDatabase({ connectionString: urlAs(other), maxConnections: 3 });
      others.push(extra);
      return extra;
    },
    async drop() {
      await handle.close();
      await Promise.all(others.map((o) => o.close()));
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
