#!/usr/bin/env node
// glob2-migrate [latest|status]
// Reads DATABASE_URL from the environment (or a .env file in the working directory).
import { existsSync } from 'node:fs';
import { createDatabase } from './connection.ts';
import { createMigrator, migrateToLatest } from './migrate.ts';

async function main(): Promise<number> {
  const command = process.argv[2] ?? 'latest';
  if (!process.env['DATABASE_URL'] && existsSync('.env')) process.loadEnvFile('.env');
  const connectionString = process.env['DATABASE_URL'];
  if (!connectionString) {
    console.error('DATABASE_URL is not set');
    return 2;
  }
  const { db, close } = createDatabase({ connectionString, applicationName: 'glob2-migrate' });
  try {
    if (command === 'latest') {
      const results = await migrateToLatest(db);
      if (results.length === 0) console.log('database is up to date');
      for (const r of results) console.log(`${r.status.padEnd(11)} ${r.migrationName}`);
      return 0;
    }
    if (command === 'status') {
      for (const m of await createMigrator(db).getMigrations()) {
        console.log(
          `${m.executedAt ? m.executedAt.toISOString() : 'pending'.padEnd(24)}  ${m.name}`,
        );
      }
      return 0;
    }
    console.error(`unknown command ${command}; use latest or status`);
    return 2;
  } catch (error) {
    console.error(error instanceof Error ? error.message : error);
    return 1;
  } finally {
    await close();
  }
}

process.exitCode = await main();
