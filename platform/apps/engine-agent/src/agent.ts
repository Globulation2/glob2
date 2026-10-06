// An engine agent serves one sim version: it leases only that version's jobs
// from platform-api, so jobs never reach a binary that would compute different
// results. The engine work itself is behind EngineRunner: HeadlessEngineRunner
// (runners.ts) runs the glob2 headless binary; unsupportedRunner remains for
// tests and for agents started without a binary.
//
// The agent holds no database credentials. Its loop: while a slot is free,
// lease a job (platform.ts); run it with blob access scoped to that lease;
// keep the lease alive while it runs; report the result, or give the job back
// for a retry.
import { hostname } from 'node:os';
import type { Logger } from '@glob2/core';
import {
  ENGINE_JOB_KINDS,
  sameSimVersion,
  simVersionKey,
  type EngineJob,
  type EngineJobKind,
  type EngineLease,
  type ErrorCode,
  type SimVersion,
} from '@glob2/protocol';
import { HttpJobBlobs, type JobBlobs } from './blobs.ts';
import { PlatformRejection, type PlatformClient } from './platform.ts';

/** A deterministic engine failure: reported to the platform, not retried. */
export class EngineJobError extends Error {
  readonly code: ErrorCode;
  constructor(code: ErrorCode, message: string) {
    super(message);
    this.name = 'EngineJobError';
    this.code = code;
  }
}

export interface EngineRunner {
  /** Job kinds this runner can execute. */
  readonly kinds: readonly EngineJobKind[];
  /**
   * Runs a job and returns its result document (checked by the platform
   * against the kind's result schema). Throw EngineJobError for failures that
   * would repeat; any other error is retried.
   */
  run(job: EngineJob, signal: AbortSignal, blobs?: JobBlobs): Promise<unknown>;
}

export const unsupportedRunner: EngineRunner = {
  kinds: ENGINE_JOB_KINDS.filter((kind) => kind !== 'validate-ai'),
  run: async (job) => {
    throw new EngineJobError(
      'unsupported',
      `${job.kind} is not available yet: this engine agent has no headless command for it`,
    );
  },
};

export interface AgentOptions {
  buildingCatalogHash?: string;
  id?: string;
  simVersion: SimVersion;
  build: string;
  runner: EngineRunner;
  platform: PlatformClient;
  logger: Logger;
  /** Jobs run in parallel (default 1). */
  concurrency?: number;
  /** Wait between lease attempts when there is no work (default 1000 ms). */
  pollMs?: number;
  /** Lease length; renewed every third of it while a job runs (default 120 s). */
  leaseSeconds?: number;
}

/** Retry delay after a failed attempt: 5 s, 10 s, 20 s … up to 5 minutes. */
export function retryDelaySeconds(attempt: number): number {
  return Math.min(300, 5 * 2 ** Math.max(0, attempt - 1));
}

export class EngineAgent {
  readonly id: string;
  private readonly options: AgentOptions;
  private readonly abort = new AbortController();
  private readonly running = new Set<Promise<void>>();
  private loop: Promise<void> | undefined;
  private wake: (() => void) | undefined;

  constructor(options: AgentOptions) {
    this.options = options;
    this.id = options.id ?? `${hostname()}-${process.pid}`;
  }

  private get leaseSeconds(): number {
    return this.options.leaseSeconds ?? 120;
  }

  /** Starts leasing and running jobs until stop(). */
  start(): void {
    this.loop ??= this.leaseLoop();
  }

  /** Stops leasing, aborts running jobs (they are released for a retry) and waits for them. */
  async stop(): Promise<void> {
    this.abort.abort();
    this.wake?.();
    await this.loop;
    await Promise.allSettled([...this.running]);
  }

  private async sleep(ms: number): Promise<void> {
    if (this.abort.signal.aborted) return;
    await new Promise<void>((resolve) => {
      const timer = setTimeout(done, ms);
      function done() {
        clearTimeout(timer);
        resolve();
      }
      this.wake = done;
    });
    this.wake = undefined;
  }

  private async leaseLoop(): Promise<void> {
    const { platform, runner, simVersion, logger } = this.options;
    const concurrency = this.options.concurrency ?? 1;
    const pollMs = this.options.pollMs ?? 1000;
    let failures = 0;
    while (!this.abort.signal.aborted) {
      if (this.running.size >= concurrency) {
        await Promise.race(this.running);
        continue;
      }
      let lease: EngineLease | undefined;
      try {
        lease = await platform.lease({
          agentId: this.id,
          simVersion,
          kinds: [...runner.kinds],
          leaseSeconds: this.leaseSeconds,
        });
        failures = 0;
      } catch (error) {
        failures++;
        logger.warn({ err: error }, 'leasing an engine job failed');
        await this.sleep(Math.min(30_000, pollMs * 2 ** Math.min(failures, 5)));
        continue;
      }
      if (!lease) {
        await this.sleep(pollMs);
        continue;
      }
      const task = this.handle(lease).finally(() => this.running.delete(task));
      this.running.add(task);
    }
  }

  /**
   * Runs one leased job. EngineJobError results are reported; other errors
   * give the job back for a retry, except on its last attempt, where the
   * failure is reported as `internal` so the platform is not left waiting for
   * a result that will never come.
   */
  async handle(lease: EngineLease): Promise<void> {
    const { platform, logger, runner, simVersion } = this.options;
    const { job, leaseToken: token, attempt, maxAttempts } = lease;
    const log = logger.child({ jobId: job.jobId, kind: job.kind, attempt });
    const controller = new AbortController();
    const onStop = () => controller.abort();
    this.abort.signal.addEventListener('abort', onStop);
    // Keep the lease while the engine works; losing it means another agent
    // may take the job, so stop working on it.
    const keepAlive = setInterval(
      () => {
        platform.extend(job.jobId, token, this.leaseSeconds).catch((error: unknown) => {
          if (error instanceof PlatformRejection) {
            log.warn({ err: error }, 'engine job lease lost; abandoning the job');
            controller.abort();
          } else {
            log.warn({ err: error }, 'extending the engine job lease failed');
          }
        });
      },
      Math.max(1000, (this.leaseSeconds * 1000) / 3),
    );
    const started = Date.now();
    try {
      if (!sameSimVersion(job.simVersion, simVersion)) {
        // Leases are asked for this sim version, so this means a platform bug.
        throw new Error(
          `job for ${simVersionKey(job.simVersion)} reached agent for ${simVersionKey(simVersion)}`,
        );
      }
      const result = await runner.run(job, controller.signal, new HttpJobBlobs(platform, token));
      await platform.report(job.jobId, token, { ok: true, result: result as object });
      log.info({ ms: Date.now() - started }, 'engine job succeeded');
    } catch (error) {
      if (controller.signal.aborted && !(error instanceof EngineJobError)) {
        // Stopping or lease lost: hand the job back at once.
        await platform.release(job.jobId, token, 0).catch(() => undefined);
        log.info('engine job interrupted; released for another agent');
        return;
      }
      const lastAttempt = attempt >= maxAttempts;
      if (!(error instanceof EngineJobError) && !lastAttempt) {
        log.warn({ err: error }, 'engine job attempt failed; will retry');
        await platform
          .release(job.jobId, token, retryDelaySeconds(attempt))
          .catch((e: unknown) => log.warn({ err: e }, 'releasing the engine job failed'));
        return;
      }
      const failure =
        error instanceof EngineJobError
          ? error
          : new EngineJobError(
              'internal',
              `gave up after ${attempt} attempts: ${String((error as Error)?.message ?? error).slice(0, 1800)}`,
            );
      await platform
        .report(job.jobId, token, {
          ok: false,
          error: { code: failure.code, message: failure.message.slice(0, 2000) },
        })
        .catch((e: unknown) => log.warn({ err: e }, 'reporting the engine job failure failed'));
      log.warn({ code: failure.code, err: error }, 'engine job failed');
    } finally {
      clearInterval(keepAlive);
      this.abort.signal.removeEventListener('abort', onStop);
    }
  }

  /** Announces (or refreshes) this agent so the platform knows its sim version is served. */
  async heartbeat(): Promise<void> {
    const { platform, simVersion, runner, build } = this.options;
    await platform.heartbeat({
      agentId: this.id,
      simVersion,
      kinds: [...runner.kinds],
      build,
      ...(this.options.buildingCatalogHash
        ? { buildingCatalogHash: this.options.buildingCatalogHash }
        : {}),
    });
  }

  async deregister(): Promise<void> {
    await this.options.platform.deregister(this.id);
  }
}
