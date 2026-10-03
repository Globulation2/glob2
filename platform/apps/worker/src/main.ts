// platform-worker: runs queued platform jobs on every replica and the
// scheduler (maintenance; later the matchmaker and rating sweeps) on the one
// replica holding the leader lock.
import {
  Shutdown,
  applyEngineJobResult,
  createLogger,
  loadConfig,
  prepareJobQueue,
  startJobRunner,
} from '@glob2/core';
import { LeaderElection, createDatabase } from '@glob2/db';
import { ENGINE_RESULT_TASK } from '@glob2/protocol';
import { runMaintenance } from './maintenance.ts';
import { runScheduler, type ScheduledTask } from './scheduler.ts';

const config = loadConfig();
const logger = createLogger('worker', config.logLevel);
const shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();

try {
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-worker',
    onIdleClientError: (error) => logger.warn({ err: error }, 'idle database connection failed'),
  });
  shutdown.add('database', () => database.close());
  await prepareJobQueue(database.pool, logger);

  const runner = await startJobRunner({
    pool: database.pool,
    logger,
    tasks: {
      [ENGINE_RESULT_TASK]: async (payload) => {
        const applied = await applyEngineJobResult(database.db, payload);
        if (!applied) logger.warn({ payload }, 'engine job result for an unknown or finished job');
      },
    },
  });
  shutdown.add('job runner', () => runner.stop());

  const scheduled: ScheduledTask[] = [
    { name: 'maintenance', intervalMs: 60_000, run: () => runMaintenance(database.db) },
  ];
  const leader = new LeaderElection({
    connectionString: config.databaseUrl,
    name: 'scheduler',
    logger,
    lead: (signal) => runScheduler(scheduled, signal, logger),
  });
  leader.start();
  shutdown.add('scheduler', () => leader.stop());
  logger.info('platform worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'platform worker failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
