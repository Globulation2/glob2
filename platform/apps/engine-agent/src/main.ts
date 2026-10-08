import { createGeneratorExecutor } from './generatorValidation.ts';
import { createAssetSandbox, createSetValidator } from './setValidation.ts';
import { createAiValidator } from './aiValidation.ts';
// engine-agent: one image per sim version (the glob2 headless binary of that
// version plus this wrapper). It has no database or blob-store access: jobs,
// results and blobs go through platform-api's internal engine API.
// Configuration beyond the shared PlatformConfig (LOG_LEVEL, …):
//   PLATFORM_INTERNAL_URL  platform-api on the backend network (http://platform-api:8080)
//   ENGINE_AGENT_KEY_FILE  file holding the bearer agent key (or ENGINE_AGENT_KEY)
//   ENGINE_POLL_MS         wait between lease attempts when idle (default 1000)
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
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  ConfigError,
  ENGINE_AGENT_HEARTBEAT_SECONDS,
  Shutdown,
  createLogger,
  loadConfig,
} from '@glob2/core';
import { parseSimVersionKey, type SimVersion } from '@glob2/protocol';
import { EngineAgent, unsupportedRunner, type EngineRunner } from './agent.ts';
import { DEFAULT_LIMITS, GlobEngine } from './engine.ts';
import { PlatformClient } from './platform.ts';
import { DEFAULT_RUNNER_LIMITS, HeadlessEngineRunner } from './runners.ts';
import { describeSimVersion, detectSimVersion, SimVersionError } from './simVersion.ts';

const HEARTBEAT_MS = ENGINE_AGENT_HEARTBEAT_SECONDS * 1000;

const config = loadConfig({ database: false });
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

  const baseUrl = env['PLATFORM_INTERNAL_URL'];
  if (!baseUrl) throw new ConfigError('PLATFORM_INTERNAL_URL is required');
  let key = env['ENGINE_AGENT_KEY'];
  const keyFile = env['ENGINE_AGENT_KEY_FILE'];
  if (keyFile) {
    try {
      key = readFileSync(keyFile, 'utf8').trim();
    } catch (error) {
      throw new ConfigError(`cannot read ENGINE_AGENT_KEY_FILE: ${(error as Error).message}`);
    }
  }
  if (!key) throw new ConfigError('set ENGINE_AGENT_KEY_FILE (or ENGINE_AGENT_KEY)');
  // The key stays in this process; the engine child gets a stripped environment.
  delete process.env['ENGINE_AGENT_KEY'];
  const platform = new PlatformClient({ baseUrl, key });

  let simVersion: SimVersion;
  let buildingCatalogHash: string | undefined;
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
    buildingCatalogHash = catalog.buildingCatalogHash;
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
    let aiValidator;
    try {
      aiValidator = await createAiValidator(engine.options, simVersion);
    } catch (error) {
      logger.warn(
        { err: error },
        'AI publishing unavailable: isolated validator failed its startup probe',
      );
    }
    let generatorExecutor;
    try {
      generatorExecutor = await createGeneratorExecutor(engine.options, simVersion);
    } catch (error) {
      logger.warn(
        { err: error },
        'Generator sharing unavailable: isolated executor failed its startup probe',
      );
    }
    let setValidator;
    if (catalog.versionMinor >= 144 || catalog.commands.includes('validate_set')) {
      // Format 144 maps embed uploaded PNGs too. Publishing's opt-in only gates
      // validate-set availability; ordinary map jobs must use the same boundary.
      try {
        engine.options.processLauncher = await createAssetSandbox(engine.options);
        const validated = await createSetValidator(engine.options);
        if (env['ENGINE_SET_VALIDATION'] === '1' && catalog.commands.includes('validate_set'))
          setValidator = validated;
      } catch (error) {
        throw new ConfigError(
          'Asset-capable engine isolation failed its startup probe: ' + (error as Error).message,
        );
      }
    }
    runner = new HeadlessEngineRunner({
      ...(generatorExecutor ? { generatorExecutor } : {}),
      ...(setValidator ? { setValidator } : {}),
      ...(aiValidator ? { aiValidator } : {}),
      engine,
      catalog,
      simVersion,
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
    ...(buildingCatalogHash ? { buildingCatalogHash } : {}),
    ...(process.env['ENGINE_AGENT_ID'] ? { id: process.env['ENGINE_AGENT_ID'] } : {}),
    simVersion,
    build: process.env['ENGINE_BUILD'] ?? 'unknown',
    runner,
    platform,
    logger,
    concurrency,
    pollMs: positive('ENGINE_POLL_MS', 1000),
  });
  await agent.heartbeat();
  const heartbeat = setInterval(() => {
    agent.heartbeat().catch((error: unknown) => logger.warn({ err: error }, 'heartbeat failed'));
  }, HEARTBEAT_MS);
  shutdown.add('deregister', async () => {
    clearInterval(heartbeat);
    await agent.deregister();
  });

  agent.start();
  shutdown.add('jobs', () => agent.stop());
  logger.info({ agent: agent.id, simVersion }, 'engine agent ready');
} catch (error) {
  logger.fatal({ err: error }, 'engine agent failed to start');
  await shutdown.run('startup failure').catch(() => undefined);
  process.exit(1);
}
