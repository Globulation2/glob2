import { writeFile, rm } from 'node:fs/promises';
import { sql } from 'kysely';
import { createDatabase } from '@glob2/db';
import {
  createBlobStore,
  createLogger,
  loadConfig,
  prepareJobQueue,
  Shutdown,
  startJobRunner,
  type Task,
} from '@glob2/core';
import { MUSIC_CONVERT, MUSIC_INSPECT } from '@glob2/music';
import { processMusic } from './process.ts';
const config = loadConfig();
const logger = createLogger('music-worker', config.logLevel);
const shutdown = new Shutdown(logger, 1900);
shutdown.installSignalHandlers();
const database = createDatabase({
  connectionString: config.databaseUrl,
  applicationName: 'glob2-music-worker',
});
shutdown.add('database', () => database.close());
await prepareJobQueue(database.pool, logger);
const blobs = createBlobStore(config.blobs);
const task =
  (inspect: boolean): Task =>
  async (payload, helpers) => {
    const id = (payload as { id?: unknown })?.id;
    if (typeof id !== 'string') throw new Error('Missing music release id');
    await processMusic(
      database.db,
      blobs,
      config.publicOrigin,
      id,
      inspect,
      helpers.job.attempts >= helpers.job.max_attempts,
    );
  };
const runner = await startJobRunner({
  pool: database.pool,
  logger,
  concurrency: 1,
  tasks: {
    [MUSIC_INSPECT]: task(true),
    [MUSIC_CONVERT]: task(false),
  },
});
shutdown.add('runner', () => runner.stop());
logger.info('Music worker ready');

const heartbeat = async () => {
  try {
    await sql`SELECT 1`.execute(database.db);
    await writeFile('/tmp/glob2-music-worker-health', String(Date.now()));
  } catch (error) {
    logger.error({ err: error }, 'Music worker health check failed');
  }
};
await heartbeat();
const healthTimer = setInterval(() => {
  void heartbeat();
}, 30_000);
shutdown.add('health', async () => {
  clearInterval(healthTimer);
  await rm('/tmp/glob2-music-worker-health', { force: true });
});
