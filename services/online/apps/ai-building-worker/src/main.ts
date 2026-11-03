import { resolve } from 'node:path';
import { writeFile } from 'node:fs/promises';
import { loadConfig, createLogger, createBlobStore, Shutdown } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { BuildingAiStudio } from '@glob2/building-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { GlobEngine, DEFAULT_LIMITS } from '@glob2/engine/engine';
import { detectSimVersion } from '@glob2/engine/simVersion';
import { simVersionKey } from '@glob2/protocol';
import { Pipeline } from './pipeline.ts';
import { OpenAIBuildings } from './provider.ts';
const config = loadConfig(),
  logger = createLogger('ai-building-worker', config.logLevel),
  shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();
try {
  const cfg = config.instance.buildingStudio;
  if (
    !cfg?.enabled ||
    !cfg.textModel ||
    !cfg.imageModel ||
    cfg.pipelineVersion !== 'building-v1' ||
    !cfg.providerCallsPerDay ||
    !cfg.maxOutputTokens ||
    !cfg.timeoutSeconds
  )
    throw Error('Enable Building Studio and configure models, pipeline and budgets.');
  if (!process.env['ENGINE_BINARY'] || !process.env['GLOB2_SOURCE_DIR'])
    throw Error('Set ENGINE_BINARY and GLOB2_SOURCE_DIR.');
  const root = resolve(process.env['GLOB2_SOURCE_DIR']),
    engine = new GlobEngine({
      binary: resolve(process.env['ENGINE_BINARY']),
      workdir: root,
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 32 * 1024 * 1024,
    });
  const catalog = await engine.catalog(),
    detected = await detectSimVersion(engine, catalog, process.env);
  if (!catalog.commands.includes('compose_buildings'))
    throw Error('Engine does not support building composition.');
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-ai-building-worker',
  });
  shutdown.add('database', () => database.close());
  const pipeline = new Pipeline(
    new BuildingAiStudio(database.db),
    new AgentBlobs(createBlobStore(config.blobs), database.db),
    new OpenAIBuildings(process.env['BUILDING_OPENAI_API_KEY'] ?? ''),
    engine,
    cfg,
    root,
    simVersionKey(detected.simVersion),
  );
  const abort = new AbortController();
  let active: Promise<unknown> | undefined;
  const timer = setInterval(() => {
    if (active) return;
    active = pipeline
      .tick(abort.signal)
      .catch((e) => logger.error({ err: e }, 'building tick failed'))
      .finally(() => {
        active = undefined;
      });
  }, 2000);
  const health = setInterval(() => {
    void writeFile('/tmp/glob2-ai-building-health', String(Date.now())).catch((e) =>
      logger.error({ err: e }, 'health write failed'),
    );
  }, 10000);
  shutdown.add('building worker', async () => {
    clearInterval(timer);
    clearInterval(health);
    abort.abort();
    await active;
  });
  await writeFile('/tmp/glob2-ai-building-health', String(Date.now()));
  logger.info('Building Studio worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'Building worker startup failed');
  await shutdown.run('startup failure');
  process.exit(1);
}
