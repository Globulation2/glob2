import { resolve } from 'node:path';
import { loadConfig, createLogger, createBlobStore, prepareJobQueue, Shutdown } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { Studio } from '@glob2/map-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { GlobEngine, DEFAULT_LIMITS } from '@glob2/engine/engine';
import { detectSimVersion } from '@glob2/engine/simVersion';
import { OpenAIMaps } from './provider.ts';
import { Pipeline } from './pipeline.ts';
const config = loadConfig(),
  logger = createLogger('ai-map-worker', config.logLevel),
  shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();
try {
  const studioConfig = config.instance.mapStudio;
  if (!studioConfig?.enabled)
    throw new Error('AI Map Studio is disabled; do not start its worker.');
  if (
    !studioConfig.pipelineVersion ||
    !studioConfig.textModel ||
    !studioConfig.imageModel ||
    !studioConfig.providerCallsPerDay
  )
    throw new Error('Configure models, pipeline version, and provider budget.');
  const binary = resolve(process.env['ENGINE_BINARY'] ?? ''),
    source = resolve(process.env['GLOB2_SOURCE_DIR'] ?? '');
  if (!process.env['ENGINE_BINARY'] || !process.env['GLOB2_SOURCE_DIR'])
    throw new Error('A matching native binary and source are required.');
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-ai-map-worker',
  });
  shutdown.add('database', () => database.close());
  await prepareJobQueue(database.pool, logger);
  const engine = new GlobEngine({
    binary,
    workdir: source,
    limits: DEFAULT_LIMITS,
    maxOutputBytes: 64 * 1024 * 1024,
  });
  const catalog = await engine.catalog(),
    detected = await detectSimVersion(engine, catalog, process.env);
  const pipeline = new Pipeline({
    studio: new Studio(database.db),
    blobs: new AgentBlobs(createBlobStore(config.blobs), database.db),
    provider: new OpenAIMaps(process.env['MAP_OPENAI_API_KEY'] ?? ''),
    config: studioConfig,
    binary,
    source,
    simVersion: detected.simVersion,
    python: process.env['MAP_PYTHON'],
  });
  let active = Promise.resolve(false),
    stopped = false;
  const timer = setInterval(() => {
    if (stopped) return;
    stopped = true;
    active = pipeline
      .tick()
      .catch((error) => {
        logger.error({ err: error }, 'studio tick failed');
        return false;
      })
      .finally(() => {
        stopped = false;
      });
  }, 2000);
  shutdown.add('studio worker', async () => {
    clearInterval(timer);
    await active;
  });
  logger.info('AI map worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'AI map worker startup failed');
  await shutdown.run('startup failure');
  process.exit(1);
}
