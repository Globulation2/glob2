// An engine agent serves one sim version: it runs only that version's engine
// task identifiers, so jobs never reach a binary that would compute different
// results. The engine work itself is behind EngineRunner; the glob2 headless
// commands it calls (generation, validation, previews, --verify-match) arrive
// in later milestones, so the default runner reports every kind unsupported.
import { hostname } from 'node:os';
import { sql, type Kysely } from 'kysely';
import type { Task } from 'graphile-worker';
import { parseEngineJob, reportEngineJobResult, type JobQueue, type Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  ENGINE_JOB_KINDS,
  engineTaskIdentifier,
  sameSimVersion,
  simVersionKey,
  type EngineJob,
  type EngineJobKind,
  type ErrorCode,
  type SimVersion,
} from '@glob2/protocol';

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
   * would repeat; any other error is retried by the queue.
   */
  run(job: EngineJob, signal: AbortSignal): Promise<unknown>;
}

export const unsupportedRunner: EngineRunner = {
  kinds: ENGINE_JOB_KINDS,
  run: async (job) => {
    throw new EngineJobError(
      'unsupported',
      `${job.kind} is not available yet: this engine agent has no headless command for it`,
    );
  },
};

export interface AgentOptions {
  id?: string;
  simVersion: SimVersion;
  build: string;
  runner: EngineRunner;
  queue: JobQueue;
  db: Kysely<Database>;
  logger: Logger;
}

export class EngineAgent {
  readonly id: string;
  private readonly options: AgentOptions;
  private readonly abort = new AbortController();

  constructor(options: AgentOptions) {
    this.options = options;
    this.id = options.id ?? `${hostname()}-${process.pid}`;
  }

  /** graphile-worker task list: one task per supported kind, for this sim version only. */
  tasks(): Record<string, Task> {
    const tasks: Record<string, Task> = {};
    for (const kind of this.options.runner.kinds) {
      tasks[engineTaskIdentifier(kind, this.options.simVersion)] = (payload) =>
        this.handle(payload);
    }
    return tasks;
  }

  async handle(payload: unknown): Promise<void> {
    const { queue, logger, runner, simVersion } = this.options;
    const job = parseEngineJob(payload);
    const log = logger.child({ jobId: job.jobId, kind: job.kind });
    if (!sameSimVersion(job.simVersion, simVersion)) {
      // Task identifiers carry the sim version, so this means a routing bug.
      throw new Error(
        `job for ${simVersionKey(job.simVersion)} reached agent for ${simVersionKey(simVersion)}`,
      );
    }
    const started = Date.now();
    try {
      const result = await runner.run(job, this.abort.signal);
      await reportEngineJobResult(queue, {
        jobId: job.jobId,
        kind: job.kind,
        ok: true,
        result,
        agent: this.id,
      });
      log.info({ ms: Date.now() - started }, 'engine job succeeded');
    } catch (error) {
      if (!(error instanceof EngineJobError)) throw error;
      await reportEngineJobResult(queue, {
        jobId: job.jobId,
        kind: job.kind,
        ok: false,
        error: { code: error.code, message: error.message },
        agent: this.id,
      });
      log.warn({ code: error.code, err: error }, 'engine job failed');
    }
  }

  /** Announces (or refreshes) this agent so the platform knows its sim version is served. */
  async heartbeat(): Promise<void> {
    const { db, simVersion, runner, build } = this.options;
    await db
      .insertInto('engine_agents')
      .values({
        id: this.id,
        sim_version: simVersionKey(simVersion),
        kinds: [...runner.kinds],
        build,
      })
      .onConflict((conflict) =>
        conflict.column('id').doUpdateSet({
          sim_version: simVersionKey(simVersion),
          kinds: [...runner.kinds],
          build,
          last_seen_at: sql<Date>`now()`,
        }),
      )
      .execute();
  }

  async deregister(): Promise<void> {
    this.abort.abort();
    await this.options.db.deleteFrom('engine_agents').where('id', '=', this.id).execute();
  }
}
