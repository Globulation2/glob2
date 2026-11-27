// glob2-online crash worker (docs/multiplayer/crash-reports.md): symbolicates
// crash reports queued by the API and files them under crash groups.
//
// Environment, besides the platform's usual settings:
//   MINIDUMP_STACKWALK        minidump-stackwalk binary (default /usr/local/bin/minidump-stackwalk)
//   CRASH_SYMBOL_CACHE_DIR    decompressed symbols (default /tmp/glob2-crash-symbols)
//   CRASH_SYMBOL_CACHE_BYTES  cache size before least recently used files go (default 8 GiB)
import { mkdir, rm, writeFile } from 'node:fs/promises';
import { sql } from 'kysely';
import { createDatabase } from '@glob2/db';
import {
  CRASH_PROCESS_TASK,
  createBlobStore,
  createLogger,
  loadConfig,
  prepareJobQueue,
  Shutdown,
  startJobRunner,
} from '@glob2/core';
import { runProcess } from '@glob2/engine/process';
import { processCrashReport } from './process.ts';
import { SymbolCache } from './symbols.ts';

const config = loadConfig();
const logger = createLogger('crash-worker', config.logLevel);
const shutdown = new Shutdown(logger, 150);
shutdown.installSignalHandlers();
const abort = new AbortController();
const binary = process.env['MINIDUMP_STACKWALK'] ?? '/usr/local/bin/minidump-stackwalk';
const cacheDir = process.env['CRASH_SYMBOL_CACHE_DIR'] ?? '/tmp/glob2-crash-symbols';
const cacheBytes = Number(process.env['CRASH_SYMBOL_CACHE_BYTES'] ?? 8 * 1024 ** 3);
if (!Number.isFinite(cacheBytes) || cacheBytes <= 0)
  throw new Error('CRASH_SYMBOL_CACHE_BYTES must be a positive number of bytes');

const probe = await runProcess({
  binary,
  cwd: '/',
  args: ['--version'],
  limits: { timeoutMs: 10_000 },
});
if (probe.code !== 0) throw new Error(`${binary} does not run; install minidump-stackwalk`);
logger.info({ stackwalk: probe.stdout.trim() }, 'minidump-stackwalk found');

const database = createDatabase({
  connectionString: config.databaseUrl,
  applicationName: 'glob2-crash-worker',
});
shutdown.add('database', () => database.close());
await prepareJobQueue(database.pool, logger);
const blobs = createBlobStore(config.blobs);
await mkdir(cacheDir, { recursive: true });
const cache = new SymbolCache(cacheDir, database.db, blobs, cacheBytes);
const tools = { stackwalk: { binary, signal: abort.signal }, cache };

const runner = await startJobRunner({
  pool: database.pool,
  logger,
  concurrency: 1,
  tasks: {
    [CRASH_PROCESS_TASK]: async (payload, helpers) => {
      const id = (payload as { id?: unknown })?.id;
      if (typeof id !== 'string') throw new Error('Missing crash report id');
      await processCrashReport(
        database.db,
        blobs,
        tools,
        id,
        logger,
        helpers.job.attempts >= helpers.job.max_attempts,
      );
    },
  },
});
shutdown.add('runner', async () => {
  abort.abort();
  await runner.stop();
});
logger.info('Crash worker ready');

const heartbeat = async () => {
  try {
    await sql`SELECT 1`.execute(database.db);
    await writeFile('/tmp/glob2-crash-worker-health', String(Date.now()));
  } catch (error) {
    logger.error({ err: error }, 'Crash worker health check failed');
  }
};
await heartbeat();
const timer = setInterval(() => void heartbeat(), 30_000);
shutdown.add('health', async () => {
  clearInterval(timer);
  await rm('/tmp/glob2-crash-worker-health', { force: true });
});
