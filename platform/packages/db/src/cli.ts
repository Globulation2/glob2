#!/usr/bin/env node
// glob2-migrate [latest|status|roles]
//
//   latest  migrations (platform and job queue), then grants; run as the
//           migrator role (DATABASE_URL, password from DATABASE_PASSWORD_FILE).
//   status  lists migrations.
//   roles   creates or updates the least-privilege roles and hands existing
//           objects to the migrator (src/roles.ts); run as a superuser
//           (ADMIN_DATABASE_URL, password from ADMIN_DATABASE_PASSWORD_FILE).
//           Role passwords come from DB_<MIGRATOR|API|WORKER>_PASSWORD_FILE
//           (or DB_<ROLE>_PASSWORD); a role without one keeps its password.
//
// Reads the environment (or a .env file in the working directory).
import { existsSync, readFileSync } from 'node:fs';
import pg from 'pg';
import { createDatabase, databaseUrlFromEnv } from './connection.ts';
import { createMigrator, migrateDatabase } from './migrate.ts';
import { DB_ROLES, setupRoles, type DbRole, type RolePasswords } from './roles.ts';

function rolePasswords(env: Record<string, string | undefined>): RolePasswords {
  const passwords: RolePasswords = {};
  for (const role of Object.keys(DB_ROLES) as DbRole[]) {
    const name = `DB_${role.toUpperCase()}_PASSWORD`;
    const file = env[`${name}_FILE`];
    const value = file ? readFileSync(file, 'utf8').trim() : env[name];
    if (file && !value) throw new Error(`${name}_FILE ${file} is empty`);
    if (value) passwords[role] = value;
  }
  return passwords;
}

async function roles(env: Record<string, string | undefined>): Promise<number> {
  const admin = databaseUrlFromEnv(env, {
    url: 'ADMIN_DATABASE_URL',
    passwordFile: 'ADMIN_DATABASE_PASSWORD_FILE',
  });
  if (!admin) {
    console.error('ADMIN_DATABASE_URL is not set');
    return 2;
  }
  const client = new pg.Client({ connectionString: admin, application_name: 'glob2-roles' });
  await client.connect();
  try {
    const passwords = rolePasswords(env);
    await setupRoles(client, passwords);
    console.log(
      `roles ready: ${Object.values(DB_ROLES).join(', ')} (passwords set: ${
        Object.keys(passwords).join(', ') || 'none'
      })`,
    );
    return 0;
  } finally {
    await client.end();
  }
}

async function main(): Promise<number> {
  const command = process.argv[2] ?? 'latest';
  if (!process.env['DATABASE_URL'] && existsSync('.env')) process.loadEnvFile('.env');
  const env = process.env;
  try {
    if (command === 'roles') return await roles(env);
    const connectionString = databaseUrlFromEnv(env);
    if (!connectionString) {
      console.error('DATABASE_URL is not set');
      return 2;
    }
    const handle = createDatabase({ connectionString, applicationName: 'glob2-migrate' });
    try {
      if (command === 'latest') {
        const results = await migrateDatabase(handle);
        if (results.length === 0) console.log('database is up to date');
        for (const r of results) console.log(`${r.status.padEnd(11)} ${r.migrationName}`);
        return 0;
      }
      if (command === 'status') {
        for (const m of await createMigrator(handle.db).getMigrations()) {
          console.log(
            `${m.executedAt ? m.executedAt.toISOString() : 'pending'.padEnd(24)}  ${m.name}`,
          );
        }
        return 0;
      }
      console.error(`unknown command ${command}; use latest, status or roles`);
      return 2;
    } finally {
      await handle.close();
    }
  } catch (error) {
    console.error(error instanceof Error ? error.message : error);
    return 1;
  }
}

process.exitCode = await main();
