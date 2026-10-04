// Least-privilege roles (src/roles.ts): what each service role can and cannot
// do, and the upgrade of a database that the superuser migrated before roles
// existed (every instance deployed until now).
import { mkdtempSync, readdirSync, copyFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { Logger, runMigrations } from 'graphile-worker';
import pg from 'pg';
import {
  DB_ROLES,
  MIGRATIONS_DIR,
  createMigrator,
  migrateDatabase,
  prepareDatabase,
} from '../src/index.ts';
import { createTestDatabase, type TestDatabase } from './support.ts';

const silent = new Logger(() => () => undefined);

async function attempt(url: string, statement: string, params: unknown[] = []) {
  const client = new pg.Client({ connectionString: url });
  await client.connect();
  try {
    await client.query(statement, params);
    return 'ok';
  } catch (error) {
    return (error as { code?: string }).code ?? String(error);
  } finally {
    await client.end();
  }
}

const DENIED = '42501';

describe('service roles on a fresh database', () => {
  let database: TestDatabase;
  beforeAll(async () => {
    database = await createTestDatabase();
  });
  afterAll(async () => {
    await database?.drop();
  });

  it('have no elevated attributes, and the migrator owns every object', async () => {
    const { rows } = await database.pool.query<{
      rolname: string;
      rolsuper: boolean;
      rolcreaterole: boolean;
      rolcreatedb: boolean;
      rolbypassrls: boolean;
    }>(
      `SELECT rolname, rolsuper, rolcreaterole, rolcreatedb, rolbypassrls FROM pg_roles
       WHERE rolname = ANY($1) ORDER BY rolname`,
      [Object.values(DB_ROLES)],
    );
    expect(rows).toHaveLength(3);
    for (const row of rows) {
      expect([row.rolsuper, row.rolcreaterole, row.rolcreatedb, row.rolbypassrls]).toEqual([
        false,
        false,
        false,
        false,
      ]);
    }
    const owners = await database.pool.query<{ owner: string }>(
      `SELECT DISTINCT pg_get_userbyid(c.relowner) AS owner FROM pg_class c
       JOIN pg_namespace n ON n.oid = c.relnamespace
       WHERE n.nspname IN ('public', 'graphile_worker') AND c.relkind IN ('r', 'v', 'S')`,
    );
    expect(owners.rows.map((r) => r.owner)).toEqual([DB_ROLES.migrator]);
  });

  it('give the API and the worker data access but no DDL or server-side programs', async () => {
    for (const role of ['api', 'worker'] as const) {
      const url = database.urlAs(role);
      expect(await attempt(url, 'CREATE TABLE evil (x int)')).toBe(DENIED);
      expect(await attempt(url, 'ALTER TABLE accounts ADD COLUMN evil int')).not.toBe('ok');
      expect(await attempt(url, 'DROP TABLE matches')).not.toBe('ok');
      expect(await attempt(url, 'TRUNCATE matches')).toBe(DENIED);
      expect(await attempt(url, "COPY (SELECT 1) TO PROGRAM 'id'")).toBe(DENIED);
      expect(await attempt(url, 'SELECT rolpassword FROM pg_authid')).toBe(DENIED);
      expect(await attempt(url, 'CREATE SCHEMA evil')).toBe(DENIED);
      expect(await attempt(url, 'DELETE FROM platform_migrations')).toBe(DENIED);
      expect(await attempt(url, 'SELECT count(*) FROM platform_migrations')).toBe('ok');
      expect(await attempt(url, 'SELECT count(*) FROM matches')).toBe('ok');
      for (const table of ['studio_events', 'studio_artifacts']) {
        expect(await attempt(url, `SELECT count(*) FROM ${table}`)).toBe('ok');
        expect(await attempt(url, `DELETE FROM ${table} WHERE false`)).toBe('ok');
      }
      // Both enqueue and run graphile-worker jobs (its tables use row security).
      expect(await attempt(url, "SELECT graphile_worker.add_job('test:task', '{}'::json)")).toBe(
        'ok',
      );
      expect(await attempt(url, 'SELECT count(*) FROM graphile_worker._private_jobs')).toBe('ok');
    }
  });

  it('keep sign-in secrets and the audit log from the worker, and the audit log append-only', async () => {
    const worker = database.urlAs('worker');
    for (const table of ['identities', 'admin_audit_log']) {
      expect(await attempt(worker, `SELECT count(*) FROM ${table}`)).toBe(DENIED);
    }
    // Guest retention reads when a device credential was last used, not the credential.
    expect(await attempt(worker, 'SELECT account_id, last_used_at FROM device_credentials')).toBe(
      'ok',
    );
    expect(await attempt(worker, 'SELECT credential_hash FROM device_credentials')).toBe(DENIED);
    expect(await attempt(worker, 'DELETE FROM device_credentials')).toBe(DENIED);
    // Retention deletes still work, without reading the token hashes.
    expect(await attempt(worker, 'DELETE FROM refresh_tokens WHERE expires_at < now()')).toBe('ok');
    expect(await attempt(worker, 'SELECT token_hash FROM refresh_tokens')).toBe(DENIED);
    expect(await attempt(worker, 'DELETE FROM web_sessions WHERE revoked_at < now()')).toBe('ok');

    const api = database.urlAs('api');
    expect(
      await attempt(
        api,
        "INSERT INTO admin_audit_log (action, target_type, target_id) VALUES ('t', 'account', 'x')",
      ),
    ).toBe('ok');
    expect(await attempt(api, "UPDATE admin_audit_log SET action = 'forged'")).toBe(DENIED);
    expect(await attempt(api, 'DELETE FROM admin_audit_log')).toBe(DENIED);
  });
});

describe('upgrading a database the superuser migrated', () => {
  let database: TestDatabase;
  let dir: string;
  beforeAll(async () => {
    // As every instance was deployed before roles: no grants, superuser-owned.
    database = await createTestDatabase({ prepare: false, role: 'admin' });
    dir = mkdtempSync(join(tmpdir(), 'glob2-old-migrations-'));
    for (const file of readdirSync(MIGRATIONS_DIR)) {
      if (file < '0010') copyFileSync(join(MIGRATIONS_DIR, file), join(dir, file));
    }
  });
  afterAll(async () => {
    rmSync(dir, { recursive: true, force: true });
    await database?.drop();
  });

  it('hands every object to the migrator, then migrates and serves as the roles', async () => {
    const { error } = await createMigrator(database.db, dir).migrateToLatest();
    expect(error).toBeUndefined();
    await runMigrations({ pgPool: database.pool, logger: silent });
    // Old-style state: a queued engine job carried by graphile-worker.
    await database.pool.query(`
      INSERT INTO engine_jobs (id, kind, sim_version, payload, created_at) VALUES
        ('00000000-0000-4000-8000-000000000001', 'verify-match', '125-49-${'ab'.repeat(32)}', '{}', now() - interval '2 days'),
        ('00000000-0000-4000-8000-000000000002', 'generate-map', '125-49-${'ab'.repeat(32)}', '{}', now() - interval '2 days'),
        ('00000000-0000-4000-8000-000000000003', 'generate-map', '125-49-${'ab'.repeat(32)}', '{}', now())`);
    await database.pool.query(
      `SELECT graphile_worker.add_job('engine:generate-map:125-49-${'ab'.repeat(32)}', '{}'::json)`,
    );
    // The services' roles cannot even connect usefully yet.
    expect(await attempt(database.urlAs('api'), 'SELECT count(*) FROM accounts')).toBe(DENIED);

    const client = new pg.Client({ connectionString: database.urlAs('admin') });
    await client.connect();
    try {
      await prepareDatabase(client);
      await prepareDatabase(client); // idempotent
    } finally {
      await client.end();
    }
    const migrator = database.as('migrator');
    const applied = await migrateDatabase(migrator);
    expect(applied.map((r) => r.migrationName)[0]).toBe('0010_engine_job_leases');

    const objects = await database.pool.query<{ kind: string; name: string; owner: string }>(`
      SELECT 'rel' AS kind, n.nspname || '.' || c.relname AS name, pg_get_userbyid(c.relowner) AS owner
        FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname IN ('public', 'graphile_worker') AND c.relkind IN ('r', 'v', 'm', 'S')
      UNION ALL
      SELECT 'fn', p.oid::regprocedure::text, pg_get_userbyid(p.proowner)
        FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace
        WHERE n.nspname IN ('public', 'graphile_worker')
      UNION ALL
      SELECT 'type', n.nspname || '.' || t.typname, pg_get_userbyid(t.typowner)
        FROM pg_type t JOIN pg_namespace n ON n.oid = t.typnamespace
        WHERE n.nspname IN ('public', 'graphile_worker') AND t.typtype = 'd'
      UNION ALL
      SELECT 'schema', nspname, pg_get_userbyid(nspowner) FROM pg_namespace
        WHERE nspname IN ('public', 'graphile_worker')`);
    const strays = objects.rows.filter((o) => o.owner !== DB_ROLES.migrator);
    expect(strays).toEqual([]);
    expect(objects.rows.length).toBeGreaterThan(50);

    // The old graphile engine job is gone; recent queued rows and verify jobs stay.
    const tasks = await database.pool.query(
      `SELECT 1 FROM graphile_worker._private_jobs j JOIN graphile_worker._private_tasks t
       ON t.id = j.task_id WHERE t.identifier LIKE 'engine:%'`,
    );
    expect(tasks.rowCount).toBe(0);
    const jobs = await database.pool.query<{ id: string; status: string }>(
      'SELECT id, status FROM engine_jobs ORDER BY id',
    );
    expect(jobs.rows.map((r) => r.status)).toEqual(['queued', 'failed', 'queued']);

    for (const role of ['api', 'worker'] as const) {
      const url = database.urlAs(role);
      expect(await attempt(url, 'SELECT count(*) FROM matches')).toBe('ok');
      for (const table of ['studio_events', 'studio_artifacts']) {
        expect(await attempt(url, `SELECT count(*) FROM ${table}`)).toBe('ok');
        expect(await attempt(url, `DELETE FROM ${table} WHERE false`)).toBe('ok');
      }
      expect(
        await attempt(url, "INSERT INTO accounts (kind, display_name) VALUES ('guest', $1)", [
          `Guest ${role}`,
        ]),
      ).toBe('ok');
      expect(await attempt(url, 'CREATE TABLE evil (x int)')).toBe(DENIED);
    }
  });
});
