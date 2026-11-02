// Runs one glob2 headless command as a child process with a wall-clock
// timeout, OS resource limits, a private scratch directory and bounded output
// capture. Engine commands are untrusted in the sense that a malformed map or
// record may make them loop, allocate without bound or crash; none of that may
// take the agent down with it.
import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export interface ProcessLimits {
  /** Wall-clock limit; the process group is killed (SIGKILL) when it passes. */
  timeoutMs: number;
  /** RLIMIT_CPU in seconds (ulimit -t); 0 = unlimited. */
  cpuSeconds?: number;
  /** Address-space limit in MiB (ulimit -v), applied on Linux only; 0 = unlimited. */
  memoryMb?: number;
  /** Largest file the process may write, in MiB (ulimit -f); 0 = unlimited. */
  fileSizeMb?: number;
}

export interface RunOptions {
  binary: string;
  args: readonly string[];
  /** Working directory (where the engine finds its data/ directory). */
  cwd: string;
  /** Extra environment on top of a minimal inherited one. */
  env?: Record<string, string>;
  limits: ProcessLimits;
  signal?: AbortSignal;
  /** Bytes of stdout and of stderr kept (the tail is kept); default 64 KiB. */
  maxCaptureBytes?: number;
  /** Trusted subprocess progress; caller must bound and validate each record. */
  onStdout?: (chunk: Buffer) => void;
}

export interface RunResult {
  /** Exit code, or null when the process was killed by a signal. */
  code: number | null;
  signal: NodeJS.Signals | null;
  timedOut: boolean;
  stdout: string;
  stderr: string;
  ms: number;
}

/** Environment variables a child may inherit; everything else (credentials, DATABASE_URL) is dropped. */
const INHERITED_ENV = [
  'PATH',
  'LANG',
  'LC_ALL',
  'TZ',
  'TMPDIR',
  'DISPLAY',
  'LD_LIBRARY_PATH',
  'DYLD_LIBRARY_PATH',
];

/** POSIX sh wrapper that applies the limits and then execs the engine. */
export function limitPrefix(limits: ProcessLimits): string[] {
  const steps: string[] = [];
  if (limits.cpuSeconds) steps.push(`ulimit -t ${Math.ceil(limits.cpuSeconds)}`);
  if (limits.fileSizeMb) steps.push(`ulimit -f ${Math.ceil(limits.fileSizeMb * 2048)}`); // 512-byte blocks
  if (limits.memoryMb) {
    // macOS does not enforce RLIMIT_AS (setting it fails); Linux images do.
    steps.push(
      `if [ "$(uname)" = Linux ]; then ulimit -v ${Math.ceil(limits.memoryMb * 1024)}; fi`,
    );
  }
  if (steps.length === 0) return [];
  return ['/bin/sh', '-c', `${steps.join(' && ')} && exec "$0" "$@"`];
}

class TailBuffer {
  private chunks: Buffer[] = [];
  private size = 0;
  truncated = false;
  private readonly limit: number;
  constructor(limit: number) {
    this.limit = limit;
  }
  push(chunk: Buffer): void {
    this.chunks.push(chunk);
    this.size += chunk.length;
    while (this.size > this.limit && this.chunks.length > 1) {
      this.size -= this.chunks.shift()?.length ?? 0;
      this.truncated = true;
    }
  }
  text(): string {
    let all = Buffer.concat(this.chunks);
    if (all.length > this.limit) {
      all = all.subarray(all.length - this.limit);
      this.truncated = true;
    }
    return (this.truncated ? '…' : '') + all.toString('utf8');
  }
}

export async function runProcess(options: RunOptions): Promise<RunResult> {
  const env: Record<string, string> = {};
  for (const name of INHERITED_ENV) {
    const value = process.env[name];
    if (value !== undefined) env[name] = value;
  }
  Object.assign(env, options.env);
  const prefix = limitPrefix(options.limits);
  const [command = options.binary, ...args] = prefix.length
    ? [...prefix, options.binary, ...options.args]
    : [options.binary, ...options.args];
  const capture = options.maxCaptureBytes ?? 64 * 1024;
  const stdout = new TailBuffer(capture);
  const stderr = new TailBuffer(capture);
  const started = Date.now();

  return new Promise<RunResult>((resolve, reject) => {
    if (options.signal?.aborted) {
      reject(new Error('aborted before start'));
      return;
    }
    const child = spawn(command, args, {
      cwd: options.cwd,
      env,
      stdio: ['ignore', 'pipe', 'pipe'],
      // Own process group, so a timeout kills anything the engine spawned too.
      detached: true,
    });
    let timedOut = false;
    let settled = false;
    const kill = () => {
      try {
        if (child.pid) process.kill(-child.pid, 'SIGKILL');
      } catch {
        child.kill('SIGKILL');
      }
    };
    const timer = setTimeout(() => {
      timedOut = true;
      kill();
    }, options.limits.timeoutMs);
    const onAbort = () => kill();
    options.signal?.addEventListener('abort', onAbort, { once: true });
    child.stdout.on('data', (chunk: Buffer) => {
      stdout.push(chunk);
      options.onStdout?.(chunk);
    });
    child.stderr.on('data', (chunk: Buffer) => stderr.push(chunk));
    const finish = (fn: () => void) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      options.signal?.removeEventListener('abort', onAbort);
      fn();
    };
    child.on('error', (error) => finish(() => reject(error)));
    child.on('close', (code, signal) =>
      finish(() =>
        resolve({
          code,
          signal,
          timedOut,
          stdout: stdout.text(),
          stderr: stderr.text(),
          ms: Date.now() - started,
        }),
      ),
    );
  });
}

/** Creates a private scratch directory, runs fn in it, and always removes it. */
export async function withScratchDir<T>(
  root: string | undefined,
  fn: (dir: string) => Promise<T>,
): Promise<T> {
  const dir = await mkdtemp(join(root ?? tmpdir(), 'glob2-job-'));
  try {
    return await fn(dir);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}

/** Last lines of a process's output, for error messages. */
export function outputTail(result: Pick<RunResult, 'stdout' | 'stderr'>, maxChars = 1500): string {
  const text = [result.stderr.trim(), result.stdout.trim()].filter(Boolean).join('\n');
  return text.length > maxChars ? `…${text.slice(text.length - maxChars)}` : text;
}
