import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { resolve } from 'node:path';
import { writeFile } from 'node:fs/promises';
import { loadConfig, createLogger, createBlobStore, Shutdown } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { TerrainStudio } from '@glob2/terrain-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { GlobEngine, DEFAULT_LIMITS } from '@glob2/engine/engine';
import { detectSimVersion } from '@glob2/engine/simVersion';
import { simVersionKey } from '@glob2/protocol';
import { Pipeline } from './pipeline.ts';
import { OpenAITerrain } from './provider.ts';
const config = loadConfig(),
  logger = createLogger('ai-terrain-worker', config.logLevel),
  shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();
try {
  const cfg = config.instance.terrainStudio;
  if (
    !cfg?.enabled ||
    !cfg.textModel ||
    !cfg.imageModel ||
    cfg.pipelineVersion !== 'terrain-v1' ||
    !cfg.providerCallsPerDay ||
    !cfg.timeoutSeconds
  )
    throw Error('Enable Terrain Studio and configure models, pipeline and budgets.');
  if (
    !process.env['ENGINE_BINARY'] ||
    !process.env['GLOB2_SOURCE_DIR'] ||
    !process.env['TERRAIN_PYTHON']
  )
    throw Error('Set ENGINE_BINARY, GLOB2_SOURCE_DIR and TERRAIN_PYTHON (pinned asset encoder).');
  const encoder = await promisify(execFile)(
    process.env['TERRAIN_PYTHON'],
    ['-c', 'import PIL; print(PIL.__version__)'],
    { timeout: 10000 },
  );
  if (encoder.stdout.trim() !== '12.2.0')
    throw Error('Terrain artwork requires the pinned Pillow 12.2.0 encoder.');
  const root = resolve(process.env['GLOB2_SOURCE_DIR']),
    engine = new GlobEngine({
      binary: resolve(process.env['ENGINE_BINARY']),
      workdir: root,
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 32 * 1024 * 1024,
    });
  const catalog = await engine.catalog(),
    detected = await detectSimVersion(engine, catalog, process.env);
  if (!catalog.commands.includes('validate_set'))
    throw Error('Engine does not support set validation.');
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-ai-terrain-worker',
  });
  shutdown.add('database', () => database.close());
  const pipeline = new Pipeline(
    new TerrainStudio(database.db),
    new AgentBlobs(createBlobStore(config.blobs), database.db),
    new OpenAITerrain(process.env['TERRAIN_OPENAI_API_KEY'] ?? ''),
    engine,
    cfg,
    root,
    process.env['TERRAIN_PYTHON'],
    simVersionKey(detected.simVersion),
  );
  const abort = new AbortController();
  let active: Promise<unknown> | undefined;
  const timer = setInterval(() => {
    if (active) return;
    active = pipeline
      .tick(abort.signal)
      .catch((e) => logger.error({ err: e }, 'terrain tick failed'))
      .finally(() => {
        active = undefined;
      });
  }, 2000);
  const health = setInterval(() => {
    void writeFile('/tmp/glob2-ai-terrain-health', String(Date.now())).catch((e) =>
      logger.error({ err: e }, 'health write failed'),
    );
  }, 10000);
  shutdown.add('terrain worker', async () => {
    clearInterval(timer);
    clearInterval(health);
    abort.abort();
    await active;
  });
  await writeFile('/tmp/glob2-ai-terrain-health', String(Date.now()));
  logger.info('Terrain Studio worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'Terrain worker startup failed');
  await shutdown.run('startup failure');
  process.exit(1);
}
