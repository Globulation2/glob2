// platform-worker: runs queued platform jobs on every replica and the
// scheduler (maintenance, matchmaker, rating sweep) on the one replica
// holding the leader lock.
import {
  ConfigError,
  JobQueue,
  Shutdown,
  createAccessPolicy,
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
import { abortMatchesOnLostRelays, expireStartingMatches } from './play/intake.ts';
import { PlatformMatchStarter } from './play/start.ts';
import { applyPendingRatings, handleEngineJobResult } from './ratings/apply.ts';
import { runScheduler, type ScheduledTask } from './scheduler.ts';
import { WarmMapPool, takeWarmMap } from './warmMaps.ts';

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
  const jobs = await JobQueue.create(database.pool, logger);
  shutdown.add('job queue', () => jobs.close());

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
    // Places queue matches on relays, taking maps from the warm pool when
    // one is ready and generating on demand otherwise.
    starter: new PlatformMatchStarter({
      db: database.db,
      jobs,
      warmMaps: {
        takeWarmMap: (queueId, simVersionKey, options) =>
          takeWarmMap(database.db, queueId, simVersionKey, options),
      },
      access: createAccessPolicy(config.instance.access.policy),
      logger,
    }),
    notifier: new PgQueueNotifier(),
    logger,
  });
  const scheduled: ScheduledTask[] = [
    { name: 'maintenance', intervalMs: 60_000, run: () => runMaintenance(database.db) },
    {
      name: 'starting matches',
      intervalMs: 30_000,
      run: () => expireStartingMatches(database.db),
    },
    {
      name: 'lost relays',
      intervalMs: 30_000,
      run: async () => {
        const lost = await abortMatchesOnLostRelays(database.db);
        if (lost.length > 0) logger.warn({ matches: lost }, 'aborted matches lost with their relay');
      },
    },
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
  const warmMaps = new WarmMapPool({ db: database.db, queue: jobs, queues, perEntry, logger });
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
