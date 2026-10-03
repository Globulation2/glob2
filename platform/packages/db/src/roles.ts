// Least-privilege database roles. Nothing that runs continuously connects as a
// superuser:
//
//   glob2_migrator  owns every schema object; runs migrations (ours and
//                   graphile-worker's) from the one-shot `init` service.
//   glob2_api       platform-api: data access (DML) on the platform tables, no
//                   DDL; the admin audit log is append-only.
//   glob2_worker    platform-worker: the same, minus the tables it never
//                   needs (sign-in identities, device credentials, the audit
//                   log) and only the columns its retention deletes read.
//
// The engine agent has no database role: it talks to platform-api's internal
// HTTP API (docs/hosting/README.md, "Database roles").
//
// setupRoles() runs as a superuser (the postgres image's POSTGRES_USER) and is
// idempotent: it creates or updates the roles, moves objects a superuser owns
// (databases created before roles existed) to glob2_migrator, and applies the
// grants. applyGrants() runs again as glob2_migrator after every migration, so
// new tables get the right privileges.
import pg from 'pg';

export const DB_ROLES = {
  migrator: 'glob2_migrator',
  api: 'glob2_api',
  worker: 'glob2_worker',
} as const;

export type DbRole = keyof typeof DB_ROLES;

/** Schemas whose objects the migrator owns. */
const OWNED_SCHEMAS = ['public', 'graphile_worker'];

/** Tables services may only read (bookkeeping of the migrations themselves). */
const READ_ONLY_TABLES = ['platform_migrations', 'platform_migrations_lock'];

/**
 * Tables the worker process never touches: sign-in secrets, identities with
 * e-mail addresses, and moderation records.
 */
const WORKER_DENIED_TABLES = ['identities', 'device_credentials', 'admin_audit_log'];

/** Tables the worker only prunes: DELETE plus SELECT on the columns its WHERE reads. */
const WORKER_PRUNE_ONLY: Record<string, string[]> = {
  refresh_tokens: ['expires_at'],
  web_sessions: ['expires_at', 'revoked_at'],
  auth_flows: ['expires_at'],
};

type Queryable = Pick<pg.ClientBase, 'query'>;

const ident = (name: string) => pg.escapeIdentifier(name);

/** Creates (or updates) a login role without any elevated attribute. */
async function ensureRole(client: Queryable, role: string, password?: string): Promise<void> {
  // CREATE ROLE has no IF NOT EXISTS; a concurrent creator is fine.
  try {
    await client.query(
      `DO $$ BEGIN
         IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = ${pg.escapeLiteral(role)}) THEN
           CREATE ROLE ${ident(role)} LOGIN;
         END IF;
       END $$`,
    );
  } catch (error) {
    // Another session created it between the check and CREATE ROLE.
    const code = (error as { code?: string }).code;
    if (code !== '23505' && code !== '42710') throw error;
  }
  await client.query(
    `ALTER ROLE ${ident(role)} LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION NOBYPASSRLS` +
      (password === undefined ? '' : ` PASSWORD ${pg.escapeLiteral(password)}`),
  );
}

export interface RolePasswords {
  migrator?: string;
  api?: string;
  worker?: string;
}

/**
 * Creates the roles and hands the database to glob2_migrator. Run as a
 * superuser connected to the platform database. Passwords left out keep the
 * role's current password (or none, for a new role).
 */
export async function setupRoles(client: Queryable, passwords: RolePasswords = {}): Promise<void> {
  await ensureRoles(client, passwords);
  await prepareDatabase(client);
}

/**
 * Creates or updates the cluster-wide roles. Concurrent callers connected to
 * the same database are serialised (advisory locks are per database, so
 * parallel set-ups of several databases call this on a shared one first).
 */
export async function ensureRoles(client: Queryable, passwords: RolePasswords = {}): Promise<void> {
  await client.query('SELECT pg_advisory_lock(hashtext($1))', ['glob2-setup-roles']);
  try {
    for (const role of Object.keys(DB_ROLES) as DbRole[]) {
      await ensureRole(client, DB_ROLES[role], passwords[role]);
    }
  } finally {
    await client.query('SELECT pg_advisory_unlock(hashtext($1))', ['glob2-setup-roles']);
  }
}

/** Database privileges and object ownership for the roles; run as a superuser on that database. */
export async function prepareDatabase(client: Queryable): Promise<void> {
  const { rows } = await client.query<{ db: string }>('SELECT current_database() AS db');
  const database = String(rows[0]?.db);
  const { migrator, api, worker } = DB_ROLES;
  // Only the platform's roles (and superusers) may connect.
  await client.query(`REVOKE ALL ON DATABASE ${ident(database)} FROM PUBLIC`);
  await client.query(
    `GRANT CONNECT, TEMPORARY ON DATABASE ${ident(database)} TO ${ident(migrator)}, ${ident(api)}, ${ident(worker)}`,
  );
  // The migrator creates schemas (graphile-worker's) and objects in public.
  await client.query(`GRANT CREATE ON DATABASE ${ident(database)} TO ${ident(migrator)}`);
  await client.query(`ALTER SCHEMA public OWNER TO ${ident(migrator)}`);
  await client.query(`REVOKE CREATE ON SCHEMA public FROM PUBLIC`);
  await transferOwnership(client, migrator);
  await applyGrants(client);
}

/**
 * Moves every object in the platform schemas that someone other than the
 * migrator owns (a database migrated by the superuser before roles existed)
 * to the migrator. Objects that belong to extensions are left alone.
 */
async function transferOwnership(client: Queryable, migrator: string): Promise<void> {
  const schemas = OWNED_SCHEMAS.map((s) => pg.escapeLiteral(s)).join(', ');
  const target = pg.escapeLiteral(migrator);
  await client.query(`DO $$
DECLARE
  r record;
  owner_role oid := (SELECT oid FROM pg_roles WHERE rolname = ${target});
BEGIN
  FOR r IN SELECT n.nspname FROM pg_namespace n
           WHERE n.nspname IN (${schemas}) AND n.nspowner <> owner_role LOOP
    EXECUTE format('ALTER SCHEMA %I OWNER TO %I', r.nspname, ${target});
  END LOOP;
  -- Tables, views, materialised views, sequences (owned sequences follow their table).
  FOR r IN SELECT n.nspname, c.relname, c.relkind FROM pg_class c
           JOIN pg_namespace n ON n.oid = c.relnamespace
           WHERE n.nspname IN (${schemas}) AND c.relowner <> owner_role
             AND c.relkind IN ('r', 'p', 'v', 'm', 'S', 'f')
             AND NOT EXISTS (SELECT 1 FROM pg_depend d WHERE d.classid = 'pg_class'::regclass
                             AND d.objid = c.oid AND d.deptype IN ('e', 'a', 'i'))
  LOOP
    EXECUTE format('ALTER %s %I.%I OWNER TO %I',
      CASE r.relkind WHEN 'v' THEN 'VIEW' WHEN 'm' THEN 'MATERIALIZED VIEW'
                     WHEN 'S' THEN 'SEQUENCE' WHEN 'f' THEN 'FOREIGN TABLE' ELSE 'TABLE' END,
      r.nspname, r.relname, ${target});
  END LOOP;
  FOR r IN SELECT p.oid::regprocedure AS sig, p.prokind FROM pg_proc p
           JOIN pg_namespace n ON n.oid = p.pronamespace
           WHERE n.nspname IN (${schemas}) AND p.proowner <> owner_role
             AND NOT EXISTS (SELECT 1 FROM pg_depend d WHERE d.classid = 'pg_proc'::regclass
                             AND d.objid = p.oid AND d.deptype = 'e')
  LOOP
    EXECUTE format('ALTER %s %s OWNER TO %I',
      CASE r.prokind WHEN 'p' THEN 'PROCEDURE' WHEN 'a' THEN 'AGGREGATE' ELSE 'FUNCTION' END,
      r.sig, ${target});
  END LOOP;
  -- Domains and enum/composite types (not the row types of tables, nor array types).
  FOR r IN SELECT n.nspname, t.typname, t.typtype FROM pg_type t
           JOIN pg_namespace n ON n.oid = t.typnamespace
           WHERE n.nspname IN (${schemas}) AND t.typowner <> owner_role
             AND t.typtype IN ('d', 'e', 'c', 'r') AND t.typelem = 0
             AND (t.typrelid = 0 OR (SELECT relkind FROM pg_class WHERE oid = t.typrelid) = 'c')
             AND NOT EXISTS (SELECT 1 FROM pg_depend d WHERE d.classid = 'pg_type'::regclass
                             AND d.objid = t.oid AND d.deptype = 'e')
  LOOP
    EXECUTE format('ALTER %s %I.%I OWNER TO %I',
      CASE r.typtype WHEN 'd' THEN 'DOMAIN' ELSE 'TYPE' END, r.nspname, r.typname, ${target});
  END LOOP;
END $$`);
}

/** Lists the tables (and views) of a schema. */
async function tablesOf(client: Queryable, schema: string): Promise<string[]> {
  const { rows } = await client.query<{ name: string }>(
    `SELECT c.relname AS name FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
     WHERE n.nspname = $1 AND c.relkind IN ('r', 'p', 'v', 'm') ORDER BY 1`,
    [schema],
  );
  return rows.map((r) => r.name);
}

async function schemaExists(client: Queryable, schema: string): Promise<boolean> {
  const { rows } = await client.query('SELECT 1 FROM pg_namespace WHERE nspname = $1', [schema]);
  return rows.length > 0;
}

/**
 * Grants the service roles their privileges on everything that exists now,
 * and default privileges for what the migrator creates later. Idempotent; run
 * as the migrator (or a superuser) after migrations.
 */
export async function applyGrants(client: Queryable): Promise<void> {
  const { migrator, api, worker } = DB_ROLES;
  const services = `${ident(api)}, ${ident(worker)}`;
  const dml = 'SELECT, INSERT, UPDATE, DELETE';

  // public: the platform's tables.
  await client.query(`GRANT USAGE ON SCHEMA public TO ${services}`);
  await client.query(`GRANT ${dml} ON ALL TABLES IN SCHEMA public TO ${services}`);
  await client.query(`GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO ${services}`);
  await client.query(`GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO ${services}`);
  await client.query(
    `ALTER DEFAULT PRIVILEGES FOR ROLE ${ident(migrator)} IN SCHEMA public GRANT ${dml} ON TABLES TO ${services}`,
  );
  await client.query(
    `ALTER DEFAULT PRIVILEGES FOR ROLE ${ident(migrator)} IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO ${services}`,
  );

  const tables = new Set(await tablesOf(client, 'public'));
  for (const table of READ_ONLY_TABLES) {
    if (!tables.has(table)) continue;
    await client.query(
      `REVOKE INSERT, UPDATE, DELETE, TRUNCATE ON ${ident(table)} FROM ${services}`,
    );
  }
  // The audit log is append-only for the API; deletion scrubs it through
  // scrub_audit_log_account() (SECURITY DEFINER, owned by the migrator).
  if (tables.has('admin_audit_log')) {
    await client.query(`REVOKE UPDATE, DELETE, TRUNCATE ON admin_audit_log FROM ${ident(api)}`);
  }
  // Only account deletion (the API) scrubs the audit log.
  const { rows: scrub } = await client.query(
    `SELECT 1 FROM pg_proc WHERE proname = 'scrub_audit_log_account'`,
  );
  if (scrub.length > 0) {
    await client.query(
      `REVOKE EXECUTE ON FUNCTION scrub_audit_log_account(uuid, text[]) FROM PUBLIC, ${ident(worker)}`,
    );
    await client.query(
      `GRANT EXECUTE ON FUNCTION scrub_audit_log_account(uuid, text[]) TO ${ident(api)}`,
    );
  }
  for (const table of WORKER_DENIED_TABLES) {
    if (!tables.has(table)) continue;
    await client.query(`REVOKE ALL ON ${ident(table)} FROM ${ident(worker)}`);
  }
  for (const [table, columns] of Object.entries(WORKER_PRUNE_ONLY)) {
    if (!tables.has(table)) continue;
    await client.query(`REVOKE ALL ON ${ident(table)} FROM ${ident(worker)}`);
    await client.query(
      `GRANT SELECT (${columns.map(ident).join(', ')}), DELETE ON ${ident(table)} TO ${ident(worker)}`,
    );
  }

  // graphile_worker: the job queue both services enqueue to and run from.
  if (await schemaExists(client, 'graphile_worker')) {
    await client.query(`GRANT USAGE ON SCHEMA graphile_worker TO ${services}`);
    await client.query(`GRANT ${dml} ON ALL TABLES IN SCHEMA graphile_worker TO ${services}`);
    await client.query(
      `GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA graphile_worker TO ${services}`,
    );
    await client.query(`GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA graphile_worker TO ${services}`);
    await client.query(
      `ALTER DEFAULT PRIVILEGES FOR ROLE ${ident(migrator)} IN SCHEMA graphile_worker GRANT ${dml} ON TABLES TO ${services}`,
    );
    await client.query(
      `ALTER DEFAULT PRIVILEGES FOR ROLE ${ident(migrator)} IN SCHEMA graphile_worker GRANT USAGE, SELECT ON SEQUENCES TO ${services}`,
    );
    // graphile-worker turns on row-level security without policies for its
    // _private_* tables (only their owner may use them); let the services in.
    const { rows } = await client.query<{ name: string }>(
      `SELECT c.relname AS name FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
       WHERE n.nspname = 'graphile_worker' AND c.relkind = 'r' AND c.relrowsecurity
         AND NOT EXISTS (SELECT 1 FROM pg_policies p WHERE p.schemaname = 'graphile_worker'
                         AND p.tablename = c.relname AND p.policyname = 'glob2_services')`,
    );
    for (const { name } of rows) {
      await client.query(
        `CREATE POLICY glob2_services ON graphile_worker.${ident(name)} FOR ALL TO ${services} USING (true) WITH CHECK (true)`,
      );
    }
  }
}

/** True if every platform role exists in the cluster. */
export async function rolesExist(client: Queryable): Promise<boolean> {
  const { rows } = await client.query<{ n: number }>(
    'SELECT count(*)::int AS n FROM pg_roles WHERE rolname = ANY($1)',
    [Object.values(DB_ROLES)],
  );
  return rows[0]?.n === Object.keys(DB_ROLES).length;
}

/** Replaces the user part (and password) of a connection string. */
export function connectionStringAs(base: string, user: string, password?: string): string {
  const url = new URL(base);
  url.username = encodeURIComponent(user);
  url.password = password === undefined ? '' : encodeURIComponent(password);
  return url.toString();
}
