// platform-worker: runs queued platform jobs on every replica and the
// scheduler (maintenance, matchmaker, rating sweep) on the one replica
// holding the leader lock.
import {
  ConfigError,
  JobQueue,
  Shutdown,
  createLogger,
  loadConfig,
  prepareJobQueue,
  resolveQueue,
  startJobRunner,
} from '@glob2/core';
import { LeaderElection, createDatabase } from '@glob2/db';
import { ENGINE_RESULT_TASK } from '@glob2/protocol';
import { runMaintenance } from './maintenance.ts';
import { Matchmaker } from './matchmaking/matchmaker.ts';
import { PgQueueNotifier } from './matchmaking/notifier.ts';
import type { MatchStarter } from './matchmaking/starter.ts';
import { applyPendingRatings, handleEngineJobResult } from './ratings/apply.ts';
import { runScheduler, type ScheduledTask } from './scheduler.ts';
import { WarmMapPool } from './warmMaps.ts';

const config = loadConfig();
const logger = createLogger('worker', config.logLevel);
const shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();

/**
 * Until rooms, relay allocation and match tickets land (M4), queue matches
 * cannot start: proposals fail after their retries and players wait again.
 */
const unavailableStarter: MatchStarter = {
  start: () => Promise.reject(new Error('match starting is not available on this instance yet')),
};

try {
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-worker',
    onIdleClientError: (error) => logger.warn({ err: error }, 'idle database connection failed'),
  });
  shutdown.add('database', () => database.close());
  await prepareJobQueue(database.pool, logger);
  const jobQueue = await JobQueue.create(database.pool, logger);
  shutdown.add('job queue', () => jobQueue.close());

  const runner = await startJobRunner({
    pool: database.pool,
    logger,
    tasks: {
      // Completes the engine job and, for verify-match, records the verdict
      // and applies ratings in one transaction.
      [ENGINE_RESULT_TASK]: async (payload) => {
        const applied = await handleEngineJobResult(database.db, payload);
        if (!applied) logger.warn({ payload }, 'engine job result for an unknown or finished job');
      },
    },
  });
  shutdown.add('job runner', () => runner.stop());

  const queues = config.instance.queues.map(resolveQueue);
  const matchmaker = new Matchmaker({
    db: database.db,
    queues,
    starter: unavailableStarter,
    notifier: new PgQueueNotifier(),
    logger,
  });
  const scheduled: ScheduledTask[] = [
    { name: 'maintenance', intervalMs: 60_000, run: () => runMaintenance(database.db) },
    { name: 'rating sweep', intervalMs: 30_000, run: () => applyPendingRatings(database.db) },
  ];
  if (queues.length > 0) {
    scheduled.push({ name: 'matchmaker', intervalMs: 1000, run: () => matchmaker.tick() });
  }
  // Warm map pool: WARM_MAPS_PER_ENTRY pre-generated maps per queue map pool
  // entry and served sim version (default 1; 0 turns the pool off).
  const perEntry = Number(process.env['WARM_MAPS_PER_ENTRY'] ?? '1');
  if (!Number.isInteger(perEntry) || perEntry < 0 || perEntry > 16) {
    throw new ConfigError('WARM_MAPS_PER_ENTRY must be an integer from 0 to 16');
  }
  const warmMaps = new WarmMapPool({ db: database.db, queue: jobQueue, queues, perEntry, logger });
  scheduled.push({ name: 'warm maps', intervalMs: 10_000, run: () => warmMaps.refill() });
  const leader = new LeaderElection({
    connectionString: config.databaseUrl,
    name: 'scheduler',
    logger,
    lead: (signal) => runScheduler(scheduled, signal, logger),
  });
  leader.start();
  shutdown.add('scheduler', () => leader.stop());
  logger.info({ queues: queues.map((q) => q.id) }, 'platform worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'platform worker failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
