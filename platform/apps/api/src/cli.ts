#!/usr/bin/env node
// `platform`: operator commands run on the server.
//
//   platform admin grant <account> [--role admin|moderator]
//   platform admin revoke <account>
//   platform admin ban <account> [--reason <text>]
//   platform admin delete <account> [--reason <text>]
//   platform keys generate [--kid <id>] [--dir <directory>]
//   platform matches failed [--limit <n>]
//   platform matches reverify <match id> [--force]
//
// <account> is an account id or an exact display name. Reads DATABASE_URL and
// the rest of the configuration like the services do (.env, instance.yaml).
import { existsSync, mkdirSync, realpathSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { loadConfig } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { AccountService } from './auth/accounts.ts';
import { AdminService, type Role } from './auth/admin.ts';
import { generateSigningKeyPem } from './auth/keys.ts';
import { HttpError } from './errors.ts';
import { reverify } from './history/reverify.ts';

const USAGE = `usage:
  platform admin grant <account> [--role admin|moderator]
  platform admin revoke <account>
  platform admin ban <account> [--reason <text>]
  platform admin delete <account> [--reason <text>]
  platform keys generate [--kid <id>] [--dir <directory>]
  platform matches failed [--limit <n>]
  platform matches reverify <match id> [--force]`;

export interface CliIo {
  out: (line: string) => void;
  err: (line: string) => void;
}

/** Runs one command; returns the process exit code. */
export async function runCli(
  argv: string[],
  io: CliIo = { out: console.log, err: console.error },
  env?: Record<string, string | undefined>,
): Promise<number> {
  const { positionals, values } = parseArgs({
    args: argv,
    allowPositionals: true,
    options: {
      role: { type: 'string', default: 'admin' },
      reason: { type: 'string' },
      kid: { type: 'string' },
      dir: { type: 'string' },
      force: { type: 'boolean' },
      limit: { type: 'string' },
      help: { type: 'boolean', short: 'h' },
    },
  });
  const [group, command, reference] = positionals;
  if (values.help || !group) {
    io.out(USAGE);
    return values.help ? 0 : 2;
  }

  if (group === 'keys' && command === 'generate') {
    const kid = values.kid ?? `k${new Date().toISOString().slice(0, 10).replace(/-/g, '')}`;
    const pem = generateSigningKeyPem();
    if (!values.dir) {
      io.out(pem.trimEnd());
      return 0;
    }
    mkdirSync(values.dir, { recursive: true });
    const path = join(values.dir, `${kid}.pem`);
    if (existsSync(path)) {
      io.err(`${path} already exists`);
      return 1;
    }
    writeFileSync(path, pem, { mode: 0o600 });
    io.out(`wrote ${path}`);
    return 0;
  }

  if (group === 'matches' && (command === 'failed' || (command === 'reverify' && reference))) {
    const config = loadConfig(env ? { env } : {});
    const database = createDatabase({ connectionString: config.databaseUrl, maxConnections: 2 });
    try {
      if (command === 'failed') {
        const limit = Math.min(Math.max(Number(values.limit ?? 50) || 50, 1), 1000);
        // Matches whose verification failed, and ended matches still pending
        // with no verify job in flight (the stale-job sweep marks those failed).
        const rows = await database.db
          .selectFrom('matches as m')
          .select(['m.id', 'm.verification', 'm.ended_at', 'm.queue_id', 'm.origin'])
          .where('m.status', '=', 'ended')
          .where('m.verification', 'in', ['failed', 'pending'])
          .where(({ eb, not, exists, selectFrom }) =>
            eb.or([
              eb('m.verification', '=', 'failed'),
              not(
                exists(
                  selectFrom('engine_jobs as j')
                    .select('j.id')
                    .whereRef('j.match_id', '=', 'm.id')
                    .where('j.kind', '=', 'verify-match')
                    .where('j.status', '=', 'queued'),
                ),
              ),
            ]),
          )
          .orderBy('m.ended_at', 'desc')
          .limit(limit)
          .execute();
        for (const row of rows) {
          io.out(
            `${row.id}  ${row.verification}  ${row.ended_at?.toISOString() ?? '-'}  ${row.queue_id ?? row.origin}`,
          );
        }
        if (rows.length === 0) io.out('no matches need re-verification');
        return 0;
      }
      const result = await reverify(database.db, undefined, reference ?? '', {
        force: values.force === true,
      });
      io.out(`re-verifying ${reference} (was ${result.previous}): verify job ${result.jobId}`);
      return 0;
    } catch (error) {
      if (error instanceof HttpError) {
        io.err(error.message);
        return 1;
      }
      throw error;
    } finally {
      await database.close();
    }
  }

  const ACCOUNT_COMMANDS = ['grant', 'revoke', 'ban', 'delete'];
  if (group === 'admin' && command && ACCOUNT_COMMANDS.includes(command) && reference) {
    const role = (command === 'revoke' ? 'user' : values.role) as Role;
    if (!['admin', 'moderator', 'user'].includes(role)) {
      io.err('--role must be admin or moderator');
      return 2;
    }
    const config = loadConfig(env ? { env } : {});
    const database = createDatabase({ connectionString: config.databaseUrl, maxConnections: 2 });
    try {
      const accounts = new AccountService(database.db);
      const admin = new AdminService(database.db, accounts, { endSessions: async () => undefined });
      const matches = await accounts.findByIdOrName(reference);
      if (matches.length === 0) {
        io.err(`no account ${reference}`);
        return 1;
      }
      const [target] = matches;
      if (!target || matches.length > 1) {
        io.err(`${matches.length} accounts are named ${reference}; use the account id:`);
        for (const m of matches) io.err(`  ${m.id}  ${m.kind}  ${m.display_name}`);
        return 1;
      }
      if (command === 'ban') {
        const updated = await admin.setBanned(undefined, target, true, values.reason);
        io.out(`${updated.display_name} (${updated.id}) is banned`);
        return 0;
      }
      if (command === 'delete') {
        const { removedMaps } = await admin.deleteAccount(undefined, target, values.reason);
        io.out(
          `deleted ${target.display_name} (${target.id}, ${target.kind}); removed ${removedMaps} catalog map(s)`,
        );
        return 0;
      }
      const updated = await admin.setRole(undefined, target, role);
      io.out(`${updated.display_name} (${updated.id}) is now ${updated.role}`);
      return 0;
    } catch (error) {
      if (error instanceof HttpError) {
        io.err(error.message);
        return 1;
      }
      throw error;
    } finally {
      await database.close();
    }
  }

  io.err(USAGE);
  return 2;
}

const invokedDirectly =
  process.argv[1] !== undefined &&
  realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url));
if (invokedDirectly) {
  process.exitCode = await runCli(process.argv.slice(2));
}
