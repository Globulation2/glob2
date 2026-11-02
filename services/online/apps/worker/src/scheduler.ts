// Periodic work that must run on one worker replica at a time. The scheduler
// runs only while this process holds the leader lock (LeaderElection); the
// matchmaker (M6) and rating sweeps join this list.
import type { Logger } from '@glob2/core';

export interface ScheduledTask {
  name: string;
  intervalMs: number;
  run: (signal: AbortSignal) => Promise<unknown>;
}

function sleep(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    if (signal.aborted) return resolve();
    const timer = setTimeout(done, ms);
    function done() {
      clearTimeout(timer);
      signal.removeEventListener('abort', done);
      resolve();
    }
    signal.addEventListener('abort', done);
  });
}

export interface SchedulerOptions {
  /**
   * Runs before every task run; throwing stops that run (the leader re-checks
   * its lock and lease here: LeaderElection.verify). The task waits for its
   * next interval; the scheduler ends when `signal` aborts.
   */
  guard?: () => Promise<void>;
}

/** Runs each task every intervalMs (never overlapping itself) until the signal aborts. */
export async function runScheduler(
  tasks: readonly ScheduledTask[],
  signal: AbortSignal,
  logger: Logger,
  options: SchedulerOptions = {},
): Promise<void> {
  await Promise.all(
    tasks.map(async (task) => {
      while (!signal.aborted) {
        const started = Date.now();
        try {
          await options.guard?.();
          if (signal.aborted) break;
          const result = await task.run(signal);
          logger.debug({ task: task.name, result, ms: Date.now() - started }, 'scheduled task ran');
        } catch (error) {
          logger.error({ err: error, task: task.name }, 'scheduled task failed');
        }
        await sleep(Math.max(0, task.intervalMs - (Date.now() - started)), signal);
      }
    }),
  );
}
