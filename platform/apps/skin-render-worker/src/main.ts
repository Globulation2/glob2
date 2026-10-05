import { writeFile, rm } from 'node:fs/promises';
import { sql } from 'kysely';
import { createDatabase } from '@glob2/db';
import {
  createBlobStore,
  createLogger,
  loadConfig,
  prepareJobQueue,
  startJobRunner,
  Shutdown,
  enqueueSkinSprites,
} from '@glob2/core';
import { runProcess } from '@glob2/engine/process';
import { renderSkin } from './process.ts';
import { SPRITE_BUNDLE_FORMAT } from './bundle.ts';
const config = loadConfig(),
  logger = createLogger('skin-render-worker', config.logLevel),
  shutdown = new Shutdown(logger, 310);
shutdown.installSignalHandlers();
const abort = new AbortController();
const binary = process.env['GLOB2_BINARY'] ?? '/opt/glob2/bin/glob2',
  cwd = process.env['GLOB2_DATA_DIR'] ?? '/opt/glob2/share';
const info = await runProcess({
  binary,
  cwd,
  args: ['--skin-render-info'],
  limits: { timeoutMs: 10000, memoryMb: 2048 },
});
if (info.code !== 0) throw new Error('Skin renderer capability probe failed');
const capability = JSON.parse(info.stdout) as { renderRevision: string; format: string };
if (capability.format !== SPRITE_BUNDLE_FORMAT || !/^[0-9a-f]{64}$/.test(capability.renderRevision))
  throw new Error('Unsupported skin renderer');
const revision = capability.renderRevision;
const database = createDatabase({
  connectionString: config.databaseUrl,
  applicationName: 'glob2-skin-render-worker',
});
shutdown.add('database', () => database.close());
await prepareJobQueue(database.pool, logger);
const blobs = createBlobStore(config.blobs);
await database.db.transaction().execute(async (trx) => {
  await trx
    .insertInto('skin_render_revisions')
    .values({ revision })
    .onConflict((oc) => oc.column('revision').doNothing())
    .execute();
  const versions = await trx
    .selectFrom('colony_skin_versions as v')
    .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
    .select('v.id')
    .where('s.disabled_at', 'is', null)
    .execute();
  for (const version of versions) await enqueueSkinSprites(trx, version.id, revision);
});
const runner = await startJobRunner({
  pool: database.pool,
  logger,
  concurrency: 1,
  tasks: {
    [`skin:render:${revision}`]: async (payload, helpers) => {
      const id = (payload as { id?: unknown })?.id;
      if (typeof id !== 'string') throw new Error('Missing skin derivative id');
      await renderSkin(
        database.db,
        blobs,
        { binary, cwd, revision, signal: abort.signal, attempt: helpers.job.attempts },
        id,
        logger,
        helpers.job.attempts >= helpers.job.max_attempts,
      );
    },
  },
});
shutdown.add('runner', async () => {
  abort.abort();
  await runner.stop();
});
const heartbeat = async () => {
  await database.db
    .updateTable('skin_render_revisions')
    .set({ last_seen_at: sql`now()` })
    .where('revision', '=', revision)
    .execute();
  await writeFile('/tmp/glob2-skin-render-health', String(Date.now()));
};
await heartbeat();
const timer = setInterval(() => {
  void heartbeat().catch((err) => logger.error({ err }, 'Skin worker health check failed'));
}, 30000);
shutdown.add('health', async () => {
  clearInterval(timer);
  await rm('/tmp/glob2-skin-render-health', { force: true });
});
logger.info({ revision }, 'Skin render worker ready');
