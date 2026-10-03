// engine-agent: one image per sim version (the glob2 headless binary of that
// version plus this wrapper). Configuration beyond the shared PlatformConfig:
//   ENGINE_SIM_VERSION   simVersionKey of the bundled binary (required)
//   ENGINE_BUILD         build label reported to the platform (default "unknown")
//   ENGINE_CONCURRENCY   jobs run in parallel (default 1)
//   ENGINE_AGENT_ID      stable id (default <hostname>-<pid>)
import {
  ConfigError,
  JobQueue,
  Shutdown,
  createLogger,
  loadConfig,
  prepareJobQueue,
  startJobRunner,
} from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { parseSimVersionKey } from '@glob2/protocol';
import { EngineAgent, unsupportedRunner } from './agent.ts';

const HEARTBEAT_MS = 60_000;

const config = loadConfig();
const logger = createLogger('engine-agent', config.logLevel);
const shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();

try {
  const simVersion = parseSimVersionKey(process.env['ENGINE_SIM_VERSION'] ?? '');
  if (!simVersion) {
    throw new ConfigError('ENGINE_SIM_VERSION must be a sim version key <minor>-<net>-<sha256>');
  }
  const concurrency = Number(process.env['ENGINE_CONCURRENCY'] ?? '1');
  if (!Number.isInteger(concurrency) || concurrency < 1) {
    throw new ConfigError('ENGINE_CONCURRENCY must be a positive integer');
  }

  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-engine-agent',
    maxConnections: concurrency + 4,
    onIdleClientError: (error) => logger.warn({ err: error }, 'idle database connection failed'),
  });
  shutdown.add('database', () => database.close());
  await prepareJobQueue(database.pool, logger);
  const queue = await JobQueue.create(database.pool, logger);
  shutdown.add('job queue', () => queue.close());

  const agent = new EngineAgent({
    ...(process.env['ENGINE_AGENT_ID'] ? { id: process.env['ENGINE_AGENT_ID'] } : {}),
    simVersion,
    build: process.env['ENGINE_BUILD'] ?? 'unknown',
    runner: unsupportedRunner,
    queue,
    db: database.db,
    logger,
  });
  await agent.heartbeat();
  const heartbeat = setInterval(() => {
    agent.heartbeat().catch((error: unknown) => logger.warn({ err: error }, 'heartbeat failed'));
  }, HEARTBEAT_MS);
  shutdown.add('deregister', async () => {
    clearInterval(heartbeat);
    await agent.deregister();
  });

  const runner = await startJobRunner({
    pool: database.pool,
    logger,
    tasks: agent.tasks(),
    concurrency,
  });
  shutdown.add('job runner', () => runner.stop());
  logger.info({ agent: agent.id, simVersion }, 'engine agent ready');
} catch (error) {
  logger.fatal({ err: error }, 'engine agent failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
