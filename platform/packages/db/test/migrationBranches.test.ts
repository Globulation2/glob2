import { mkdtemp, readdir, rm, symlink } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import { MIGRATIONS_DIR, createMigrator } from '../src/migrate.ts';
import { createTestDatabase } from './support.ts';

const ADMIN = [
  '0053_admin_console.sql',
  '0054_admin_analytics.sql',
  '0055_admin_finances.sql',
  '0056_admin_rollup_state.sql',
];
const STUDIO = '0053_studio_draft_history.sql';

for (const missing of [[STUDIO], ADMIN]) {
  it(`integrates missing ${missing.join(', ')} without renaming deployed migrations`, async () => {
    const database = await createTestDatabase({ migrate: false, role: 'admin' });
    const directory = await mkdtemp(join(tmpdir(), 'glob2-migration-branches-'));
    try {
      const files = await readdir(MIGRATIONS_DIR);
      // Recreate the deployed branch before later migrations depended on both sides.
      const branchFiles = files.filter((name) => name <= '0056_admin_rollup_state.sql');
      const laterFiles = files.filter((name) => name > '0056_admin_rollup_state.sql');
      for (const file of branchFiles.filter((name) => !missing.includes(name)))
        await symlink(join(MIGRATIONS_DIR, file), join(directory, file));
      const initial = await createMigrator(database.db, directory).migrateToLatest();
      expect(initial.error).toBeUndefined();
      expect(initial.results?.every((result) => result.status === 'Success')).toBe(true);

      const account = await database.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: 'Preserved branch account' })
        .returning('id')
        .executeTakeFirstOrThrow();
      for (const file of missing) await symlink(join(MIGRATIONS_DIR, file), join(directory, file));

      const integrated = await createMigrator(database.db, directory).migrateToLatest();
      expect(integrated.error).toBeUndefined();
      expect(integrated.results?.map((result) => result.migrationName)).toEqual(
        missing.map((file) => file.replace('.sql', '')).sort(),
      );
      expect(integrated.results?.every((result) => result.status === 'Success')).toBe(true);
      // After joining the historical branches, all subsequent migrations must work.
      for (const file of laterFiles)
        await symlink(join(MIGRATIONS_DIR, file), join(directory, file));
      const latest = await createMigrator(database.db, directory).migrateToLatest();
      expect(latest.error).toBeUndefined();
      expect(latest.results?.map((result) => result.migrationName)).toEqual(
        laterFiles.map((file) => file.replace('.sql', '')).sort(),
      );
      expect(latest.results?.every((result) => result.status === 'Success')).toBe(true);
      expect(await createMigrator(database.db, directory).migrateToLatest()).toEqual({
        results: [],
      });
      expect(
        await database.db
          .selectFrom('accounts')
          .select('display_name')
          .where('id', '=', account.id)
          .executeTakeFirstOrThrow(),
      ).toEqual({ display_name: 'Preserved branch account' });

      // Even with unordered additive upgrades, removing an applied migration is
      // an error: accepting incomplete history would hide accidental rollbacks.
      await rm(join(directory, '0001_initial.sql'));
      const incomplete = await createMigrator(database.db, directory).migrateToLatest();
      expect(incomplete.error).toBeDefined();
    } finally {
      await database.drop();
      await rm(directory, { recursive: true, force: true });
    }
  });
}
