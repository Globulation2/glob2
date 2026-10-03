#!/usr/bin/env node
// `platform`: operator commands run on the server.
//
//   platform admin grant <account> [--role admin|moderator]
//   platform admin revoke <account>
//   platform keys generate [--kid <id>] [--dir <directory>]
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

const USAGE = `usage:
  platform admin grant <account> [--role admin|moderator]
  platform admin revoke <account>
  platform keys generate [--kid <id>] [--dir <directory>]`;

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
      kid: { type: 'string' },
      dir: { type: 'string' },
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

  if (group === 'admin' && (command === 'grant' || command === 'revoke') && reference) {
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
