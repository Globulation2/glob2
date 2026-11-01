// Job queue on Postgres via graphile-worker: durable, retried, no extra
// service. The API enqueues; apps/worker and apps/engine-agent run tasks.
import {
  Logger as WorkerLogger,
  makeWorkerUtils,
  run,
  runMigrations,
  type Runner,
  type Task,
  type TaskSpec,
  type WorkerUtils,
} from 'graphile-worker';
import type pg from 'pg';
import type { Logger } from './logging.ts';

export type { Task, TaskSpec };

/** Routes graphile-worker's own log lines into the service logger. */
export function workerLogger(logger: Logger): WorkerLogger {
  return new WorkerLogger((scope) => (level, message, meta) => {
    const fields = { ...scope, ...(meta ?? {}) };
    if (level === 'error') logger.error(fields, message);
    else if (level === 'warning') logger.warn(fields, message);
    else if (level === 'info') logger.info(fields, message);
    else logger.debug(fields, message);
  });
}

/** Installs or upgrades graphile-worker's schema; safe to call from every process. */
export async function prepareJobQueue(pool: pg.Pool, logger: Logger): Promise<void> {
  await runMigrations({ pgPool: pool, logger: workerLogger(logger) });
}

export class JobQueue {
  private readonly utils: WorkerUtils;

  private constructor(utils: WorkerUtils) {
    this.utils = utils;
  }

  static async create(pool: pg.Pool, logger: Logger): Promise<JobQueue> {
    return new JobQueue(await makeWorkerUtils({ pgPool: pool, logger: workerLogger(logger) }));
  }

  async enqueue(task: string, payload: unknown, spec?: TaskSpec): Promise<string> {
    const job = await this.utils.addJob(task, payload as never, spec);
    return job.id;
  }

  async close(): Promise<void> {
    await this.utils.release();
  }
}

export interface JobRunnerOptions {
  pool: pg.Pool;
  logger: Logger;
  tasks: Record<string, Task>;
  concurrency?: number;
  /** Poll interval when LISTEN/NOTIFY wake-ups are missed (default 2000 ms). */
  pollIntervalMs?: number;
}

/** Starts running the given tasks; stop() waits for running jobs to finish. */
export async function startJobRunner(options: JobRunnerOptions): Promise<Runner> {
  return run(
    {
      pgPool: options.pool,
      logger: workerLogger(options.logger),
      taskList: options.tasks,
      concurrency: options.concurrency ?? 4,
      pollInterval: options.pollIntervalMs ?? 2000,
      noHandleSignals: true,
    },
    undefined,
    // No graphile cron (and no crontab file lookup): schedules live in
    // apps/worker's leader-only scheduler.
    [],
  );
}
