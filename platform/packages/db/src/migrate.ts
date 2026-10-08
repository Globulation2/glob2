import { readdir, readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Logger as GraphileLogger, runMigrations as runGraphileMigrations } from 'graphile-worker';
import { sql, type Kysely } from 'kysely';
import {
  Migrator,
  type Migration,
  type MigrationProvider,
  type MigrationResult,
} from 'kysely/migration';
import type pg from 'pg';
import { applyGrants, rolesExist } from './roles.ts';

export const MIGRATIONS_DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'migrations');

/** Plain-SQL, forward-only migrations: every `NNNN_name.sql` file, in name order. */
export class SqlFileMigrationProvider implements MigrationProvider {
  private readonly directory: string;
  constructor(directory: string = MIGRATIONS_DIR) {
    this.directory = directory;
  }

  async getMigrations(): Promise<Record<string, Migration>> {
    const files = (await readdir(this.directory)).filter((f) => /^\d{4}_[a-z0-9_]+\.sql$/.test(f));
    const migrations: Record<string, Migration> = {};
    for (const file of files.sort()) {
      const text = await readFile(join(this.directory, file), 'utf8');
      migrations[file.replace(/\.sql$/, '')] = {
        // A parameterless query runs as a simple query, so a file may hold
        // several statements. Kysely wraps each migration in a transaction.
        up: async (db) => {
          await sql.raw(text).execute(db);
        },
      };
    }
    return migrations;
  }
}

// Migrations operate below the typed schema.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AnyKysely = Kysely<any>;

export function createMigrator(db: AnyKysely, directory?: string): Migrator {
  return new Migrator({
    db,
    provider: new SqlFileMigrationProvider(directory),
    // Independently deployed branches can introduce an additive migration whose
    // name sorts before one already applied. Keep their recorded names intact;
    // Kysely still rejects missing history and serializes pending migrations.
    allowUnorderedMigrations: true,
    migrationTableName: 'platform_migrations',
    migrationLockTableName: 'platform_migrations_lock',
  });
}

/** Applies all pending migrations; throws if any fails. */
export async function migrateToLatest(db: AnyKysely): Promise<MigrationResult[]> {
  const { error, results } = await createMigrator(db).migrateToLatest();
  if (error) {
    const failed = results?.find((r) => r.status === 'Error');
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`migration ${failed?.migrationName ?? '(unknown)'} failed: ${message}`, {
      cause: error,
    });
  }
  return results ?? [];
}

/**
 * Brings a database fully up to date, as the migrator role: the platform's
 * migrations, graphile-worker's job-queue schema, then the service roles'
 * grants on whatever now exists (see roles.ts). Services never run DDL.
 * Without the roles (a development database migrated by its superuser) the
 * grants are skipped.
 */
export async function migrateDatabase(
  handle: { db: AnyKysely; pool: pg.Pool },
  options: { grants?: boolean } = {},
): Promise<MigrationResult[]> {
  const results = await migrateToLatest(handle.db);
  await runGraphileMigrations({ pgPool: handle.pool, logger: SILENT_GRAPHILE_LOGGER });
  if (options.grants !== false) {
    const client = await handle.pool.connect();
    try {
      if (await rolesExist(client)) await applyGrants(client);
    } finally {
      client.release();
    }
  }
  return results;
}

const SILENT_GRAPHILE_LOGGER = new GraphileLogger(() => () => undefined);
