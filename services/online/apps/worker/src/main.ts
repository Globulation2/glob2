import { expireMusic } from '@glob2/music';
// platform-worker: runs queued platform jobs on every replica and the
// scheduler (maintenance, matchmaker, rating sweep) on the one replica
// holding the leader lock.
import {
  ConfigError,
  JobQueue,
  Shutdown,
  createAccessPolicy,
  createBlobStore,
  createLogger,
  failAbandonedEngineJobs,
  loadConfig,
  prepareJobQueue,
  resolveQueue,
  startJobRunner,
} from '@glob2/core';
import { LeaderElection, assertLease, createDatabase } from '@glob2/db';
import { collectBlobs } from './blobGc.ts';
import { ENGINE_RESULT_TASK } from '@glob2/protocol';
import { runMaintenance } from './maintenance.ts';
import {
  DEFAULT_WARM_MAPS_MAX_PER_ENTRY,
  DEFAULT_WARM_MAPS_PER_ENTRY,
  PgQueueNotifier,
  PlatformMatchStarter,
  WarmMapPool,
  abortMatchesOnLostRelays,
  applyPendingRatings,
  expireStartingMatches,
  handleEngineJobResult,
  scrubSettledMatchNames,
  sweepStaleEngineJobs,
  takeWarmMap,
} from '@glob2/play';
import { Matchmaker } from './matchmaking/matchmaker.ts';
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
  const jobs = await JobQueue.create(database.pool, logger);
  shutdown.add('job queue', () => jobs.close());

  const runner = await startJobRunner({
    pool: database.pool,
    logger,
    tasks: {
      // Completes the engine job and, for verify-match, records the verdict
      // and applies ratings in one transaction.
      [ENGINE_RESULT_TASK]: async (payload) => {
        const applied = await handleEngineJobResult(database.db, payload, { logger });
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
    // Leader-only writes are fenced by the scheduler lease (election, below).
    fence: (trx) => assertLease(trx, election.currentLease),
  });
  const blobs = createBlobStore(config.blobs);
  const scheduled: ScheduledTask[] = [
    { name: 'maintenance', intervalMs: 60_000, run: () => runMaintenance(database.db) },
    { name: 'music uploads', intervalMs: 60_000, run: () => expireMusic(database.db, blobs) },
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
        if (lost.length > 0)
          logger.warn({ matches: lost }, 'aborted matches lost with their relay');
      },
    },
    { name: 'rating sweep', intervalMs: 30_000, run: () => applyPendingRatings(database.db) },
    {
      // Names of deleted accounts in matches that have settled since.
      name: 'deleted names',
      intervalMs: 60_000,
      run: async () => void (await scrubSettledMatchNames(database.db)),
    },
    {
      // Engine jobs whose agent stopped answering on the last attempt.
      name: 'abandoned engine jobs',
      intervalMs: 30_000,
      run: async () => {
        const failed = await failAbandonedEngineJobs(database.db);
        if (failed > 0) logger.warn({ failed }, 'failed engine jobs abandoned by their agent');
      },
    },
    {
      // Engine jobs nothing will finish: reported results that were lost,
      // and jobs no agent of their sim version took.
      name: 'stale engine jobs',
      intervalMs: 60_000,
      run: async () => (await sweepStaleEngineJobs(database.db, { logger })).length,
    },
    {
      name: 'blob garbage collection',
      intervalMs: 6 * 3600_000,
      run: () => collectBlobs(database.db, blobs, { logger }),
    },
  ];
  if (queues.length > 0) {
    scheduled.push({ name: 'matchmaker', intervalMs: 1000, run: () => matchmaker.tick() });
  }
  // Warm map pool: WARM_MAPS_PER_ENTRY pre-generated maps per queue map pool
  // entry and served sim version (default 2; 0 turns the pool off), rising
  // with recent demand up to WARM_MAPS_MAX_PER_ENTRY (default 8).
  const perEntry = Number(process.env['WARM_MAPS_PER_ENTRY'] ?? DEFAULT_WARM_MAPS_PER_ENTRY);
  if (!Number.isInteger(perEntry) || perEntry < 0 || perEntry > 16) {
    throw new ConfigError('WARM_MAPS_PER_ENTRY must be an integer from 0 to 16');
  }
  const maxPerEntry = Number(
    process.env['WARM_MAPS_MAX_PER_ENTRY'] ?? DEFAULT_WARM_MAPS_MAX_PER_ENTRY,
  );
  if (!Number.isInteger(maxPerEntry) || maxPerEntry < 0 || maxPerEntry > 32) {
    throw new ConfigError('WARM_MAPS_MAX_PER_ENTRY must be an integer from 0 to 32');
  }
  const warmMaps = new WarmMapPool({
    db: database.db,
    queues,
    perEntry,
    maxPerEntry,
    logger,
  });
  scheduled.push({ name: 'warm maps', intervalMs: 10_000, run: () => warmMaps.refill() });
  const election: LeaderElection = new LeaderElection({
    connectionString: config.databaseUrl,
    name: 'scheduler',
    logger,
    // Keepalive, a lock re-check every 5 s and before each task run, and an
    // epoch that fences the matchmaker's writes (packages/db leader.ts).
    fencing: true,
    lead: (signal): Promise<void> =>
      runScheduler(scheduled, signal, logger, { guard: (): Promise<void> => election.verify() }),
  });
  election.start();
  shutdown.add('scheduler', async () => {
    await election.stop();
    await matchmaker.waitForStarts(30_000);
  });
  logger.info({ queues: queues.map((q) => q.id) }, 'platform worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'platform worker failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
