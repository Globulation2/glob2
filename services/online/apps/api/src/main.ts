// platform-api: stateless, run as many replicas as needed behind Caddy.
import {
  JobQueue,
  Shutdown,
  createAccessPolicy,
  createBlobStore,
  createLogger,
  loadConfig,
  prepareJobQueue,
} from '@glob2/core';
import { PgPubSub, createDatabase } from '@glob2/db';
import { buildApp } from './app.ts';

const config = loadConfig();
const logger = createLogger('api', config.logLevel);
const shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();

try {
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-api',
    onIdleClientError: (error) => logger.warn({ err: error }, 'idle database connection failed'),
  });
  shutdown.add('database', () => database.close());

  const pubsub = new PgPubSub({ connectionString: config.databaseUrl, logger });
  shutdown.add('pubsub', () => pubsub.close());

  await prepareJobQueue(database.pool, logger);
  const jobs = await JobQueue.create(database.pool, logger);
  shutdown.add('job queue', () => jobs.close());

  const app = await buildApp({
    config,
    logger,
    db: database.db,
    pubsub,
    jobs,
    blobs: createBlobStore(config.blobs),
    access: createAccessPolicy(config.instance.access.policy),
  });
  // Closing Fastify stops accepting connections and waits for in-flight requests.
  shutdown.add('http', () => app.close());
  await app.listen({ host: config.http.host, port: config.http.port });
  logger.info(
    { instance: config.instance.name, instanceConfig: config.instanceConfigPath ?? '(defaults)' },
    'platform api ready',
  );
} catch (error) {
  logger.fatal({ err: error }, 'platform api failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
