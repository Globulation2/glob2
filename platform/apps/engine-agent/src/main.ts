// engine-agent: one image per sim version (the glob2 headless binary of that
// version plus this wrapper). Configuration beyond the shared PlatformConfig
// (DATABASE_URL, BLOB_STORE/BLOB_DIR, …):
//   ENGINE_BINARY          path of the glob2 binary; without it every job kind is
//                          reported unsupported and ENGINE_SIM_VERSION is required
//   ENGINE_WORKDIR         working directory for the binary, holding data/ (default cwd)
//   ENGINE_SCRATCH_DIR     parent of per-job scratch directories (default OS temp)
//   ENGINE_DATA_HASH       sim data hash, until the binary reports it itself
//   ENGINE_SIM_VERSION     full simVersionKey; checked against the binary
//   ENGINE_PROBE_SIM_VERSION=1  run `glob2 --sim-version` even if the catalog
//                          does not advertise it
//   ENGINE_TIMEOUT_GENERATE_S / _INSPECT_S / _VERIFY_S  wall-clock limits
//   ENGINE_MEMORY_MB       address-space limit per engine process (Linux)
//   ENGINE_MAX_MAP_BYTES / ENGINE_MAX_RECORD_BYTES      input size limits
//   ENGINE_BUILD           build label reported to the platform (default "unknown")
//   ENGINE_CONCURRENCY     jobs run in parallel (default 1)
//   ENGINE_AGENT_ID        stable id (default <hostname>-<pid>)
import { resolve } from 'node:path';
import {
  ConfigError,
  ENGINE_AGENT_HEARTBEAT_SECONDS,
  JobQueue,
  Shutdown,
  createBlobStore,
  createLogger,
  loadConfig,
  prepareJobQueue,
  startJobRunner,
} from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { parseSimVersionKey, type SimVersion } from '@glob2/protocol';
import { EngineAgent, unsupportedRunner, type EngineRunner } from './agent.ts';
import { AgentBlobs } from './blobs.ts';
import { DEFAULT_LIMITS, GlobEngine } from './engine.ts';
import { DEFAULT_RUNNER_LIMITS, HeadlessEngineRunner } from './runners.ts';
import { describeSimVersion, detectSimVersion, SimVersionError } from './simVersion.ts';

const HEARTBEAT_MS = ENGINE_AGENT_HEARTBEAT_SECONDS * 1000;

const config = loadConfig();
const logger = createLogger('engine-agent', config.logLevel);
const shutdown = new Shutdown(logger, config.shutdownGraceSeconds);
shutdown.installSignalHandlers();

function positive(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return fallback;
  const value = Number(raw);
  if (!Number.isFinite(value) || value <= 0)
    throw new ConfigError(`${name} must be a positive number`);
  return value;
}

try {
  const env = process.env;
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

  let simVersion: SimVersion;
  let runner: EngineRunner;
  const binary = env['ENGINE_BINARY'];
  if (binary) {
    const engine = new GlobEngine({
      binary: resolve(binary),
      workdir: resolve(env['ENGINE_WORKDIR'] ?? process.cwd()),
      ...(env['ENGINE_SCRATCH_DIR'] ? { scratchRoot: resolve(env['ENGINE_SCRATCH_DIR']) } : {}),
      limits: {
        catalog: DEFAULT_LIMITS.catalog,
        generate: {
          ...DEFAULT_LIMITS.generate,
          timeoutMs:
            positive('ENGINE_TIMEOUT_GENERATE_S', DEFAULT_LIMITS.generate.timeoutMs / 1000) * 1000,
          memoryMb: positive('ENGINE_MEMORY_MB', DEFAULT_LIMITS.generate.memoryMb ?? 4096),
        },
        inspect: {
          ...DEFAULT_LIMITS.inspect,
          timeoutMs:
            positive('ENGINE_TIMEOUT_INSPECT_S', DEFAULT_LIMITS.inspect.timeoutMs / 1000) * 1000,
          memoryMb: positive('ENGINE_MEMORY_MB', DEFAULT_LIMITS.inspect.memoryMb ?? 4096),
        },
        verify: {
          ...DEFAULT_LIMITS.verify,
          timeoutMs:
            positive('ENGINE_TIMEOUT_VERIFY_S', DEFAULT_LIMITS.verify.timeoutMs / 1000) * 1000,
          memoryMb: positive('ENGINE_MEMORY_MB', DEFAULT_LIMITS.verify.memoryMb ?? 8192),
        },
      },
      maxOutputBytes: positive('ENGINE_MAX_RECORD_BYTES', DEFAULT_RUNNER_LIMITS.maxRecordBytes),
    });
    const catalog = await engine.catalog();
    let resolved;
    try {
      resolved = await detectSimVersion(engine, catalog, {
        ENGINE_DATA_HASH: env['ENGINE_DATA_HASH'],
        ENGINE_SIM_VERSION: env['ENGINE_SIM_VERSION'],
        ENGINE_PROBE_SIM_VERSION: env['ENGINE_PROBE_SIM_VERSION'],
      });
    } catch (error) {
      if (error instanceof SimVersionError) throw new ConfigError(error.message);
      throw error;
    }
    simVersion = resolved.simVersion;
    logger.info({ simVersion: describeSimVersion(resolved), binary }, 'engine binary identified');
    runner = new HeadlessEngineRunner({
      engine,
      catalog,
      simVersion,
      blobs: new AgentBlobs(createBlobStore(config.blobs), database.db),
      limits: {
        maxMapBytes: positive('ENGINE_MAX_MAP_BYTES', DEFAULT_RUNNER_LIMITS.maxMapBytes),
        maxRecordBytes: positive('ENGINE_MAX_RECORD_BYTES', DEFAULT_RUNNER_LIMITS.maxRecordBytes),
      },
    });
  } else {
    const parsed = parseSimVersionKey(env['ENGINE_SIM_VERSION'] ?? '');
    if (!parsed) {
      throw new ConfigError(
        'set ENGINE_BINARY (or, for an agent without an engine, ENGINE_SIM_VERSION as <minor>-<net>-<sha256>)',
      );
    }
    simVersion = parsed;
    runner = unsupportedRunner;
    logger.warn('ENGINE_BINARY is not set: every engine job will be reported unsupported');
  }

  const agent = new EngineAgent({
    ...(process.env['ENGINE_AGENT_ID'] ? { id: process.env['ENGINE_AGENT_ID'] } : {}),
    simVersion,
    build: process.env['ENGINE_BUILD'] ?? 'unknown',
    runner,
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

  const jobRunner = await startJobRunner({
    pool: database.pool,
    logger,
    tasks: agent.tasks(),
    concurrency,
  });
  shutdown.add('job runner', () => jobRunner.stop());
  logger.info({ agent: agent.id, simVersion }, 'engine agent ready');
} catch (error) {
  logger.fatal({ err: error }, 'engine agent failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
