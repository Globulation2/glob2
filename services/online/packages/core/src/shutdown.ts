// Graceful shutdown: on SIGTERM/SIGINT run the registered steps in reverse
// registration order (stop accepting work first, close the database last),
// and exit anyway once the grace period runs out.
import type { Logger } from './logging.ts';

export interface ShutdownStep {
  name: string;
  run: () => Promise<void> | void;
}

export class Shutdown {
  private readonly steps: ShutdownStep[] = [];
  private readonly logger: Logger;
  private readonly graceMs: number;
  private running: Promise<void> | undefined;

  constructor(logger: Logger, graceSeconds: number) {
    this.logger = logger;
    this.graceMs = graceSeconds * 1000;
  }

  /** Registers a step; steps run last-registered first. */
  add(name: string, run: ShutdownStep['run']): this {
    this.steps.push({ name, run });
    return this;
  }

  /** Runs every step once; later calls return the same promise. */
  run(reason: string): Promise<void> {
    this.running ??= this.execute(reason);
    return this.running;
  }

  /** Installs SIGTERM/SIGINT handlers that shut down and exit the process. */
  installSignalHandlers(exit: (code: number) => void = (code) => process.exit(code)): void {
    const onSignal = (signal: NodeJS.Signals) => {
      const timer = setTimeout(() => {
        this.logger.error({ signal }, 'shutdown grace period expired; exiting');
        exit(1);
      }, this.graceMs);
      timer.unref();
      this.run(signal).then(
        () => exit(0),
        () => exit(1),
      );
    };
    process.once('SIGTERM', onSignal);
    process.once('SIGINT', onSignal);
  }

  private async execute(reason: string): Promise<void> {
    this.logger.info({ reason }, 'shutting down');
    let failed = false;
    for (const step of [...this.steps].reverse()) {
      try {
        await step.run();
        this.logger.debug({ step: step.name }, 'shutdown step finished');
      } catch (error) {
        failed = true;
        this.logger.error({ err: error, step: step.name }, 'shutdown step failed');
      }
    }
    this.logger.info('shutdown complete');
    if (failed) throw new Error('shutdown finished with errors');
  }
}
