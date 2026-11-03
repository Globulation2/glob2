import { resolve } from 'node:path';
import { writeFile } from 'node:fs/promises';
import { loadConfig, createLogger, createBlobStore, Shutdown } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { MusicStudio } from '@glob2/music-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { OpenAIMusic } from './provider.ts';
import { createRunner } from './runner.ts';
import { Pipeline } from './pipeline.ts';
const config = loadConfig(),
  logger = createLogger('ai-music-worker', config.logLevel),
  shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();
try {
  const cfg = config.instance.musicStudio;
  if (
    !cfg?.enabled ||
    !cfg.textModel ||
    cfg.pipelineVersion !== 'music-v1' ||
    !cfg.maxCalls ||
    !cfg.maxTotalTokens ||
    !cfg.maxOutputTokens ||
    !cfg.timeoutSeconds ||
    !cfg.providerCallsPerDay
  )
    throw Error('Enable Music Studio and configure its model, pipeline version and budgets.');
  const musicRoot = resolve(process.env['MUSIC_ROOT'] ?? '../tools/music');
  const runner = await createRunner(
    musicRoot,
    resolve(process.env['MUSIC_ASSETS'] ?? '/opt/music-assets'),
    resolve(process.env['MUSIC_SCRATCH'] ?? '/tmp'),
    process.env['MUSIC_PYTHON'] ?? '/opt/music/bin/python',
  );
  const database = createDatabase({
    connectionString: config.databaseUrl,
    applicationName: 'glob2-ai-music-worker',
  });
  shutdown.add('database', () => database.close());
  const pipeline = new Pipeline(
    new MusicStudio(database.db),
    new AgentBlobs(createBlobStore(config.blobs), database.db),
    new OpenAIMusic(process.env['MUSIC_OPENAI_API_KEY'] ?? ''),
    runner,
    cfg,
    musicRoot,
    config.publicOrigin,
  );
  const abort = new AbortController();
  let active: Promise<unknown> | undefined;
  const health = setInterval(() => {
    void writeFile('/tmp/glob2-ai-music-health', String(Date.now())).catch((error) =>
      logger.error({ err: error }, 'health write failed'),
    );
  }, 10000);
  const timer = setInterval(() => {
    if (active) return;
    active = pipeline
      .tick(abort.signal)
      .catch((error) => logger.error({ err: error }, 'music tick failed'))
      .finally(() => {
        active = undefined;
      });
  }, 2000);
  shutdown.add('music worker', async () => {
    clearInterval(timer);
    clearInterval(health);
    abort.abort();
    await active;
  });
  await writeFile('/tmp/glob2-ai-music-health', String(Date.now()));
  logger.info('CPU music worker ready');
} catch (error) {
  logger.fatal({ err: error }, 'Music worker startup failed');
  await shutdown.run('startup failure');
  process.exit(1);
}
