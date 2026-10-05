// Uploaded code only runs in a disposable Linux namespace. Never fall back to
// runProcess(binary): process limits alone are not an isolation boundary.
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir, stat, access } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  AI_CHECKS,
  AI_VALIDATION_SUITE,
  AiMetadata,
  parse,
  pendingAiReport,
  simVersionKey,
  type AiValidationReport,
  type SimVersion,
} from '@glob2/protocol';
import { runProcess, withScratchDir } from '@glob2/engine/process';
import type { EngineOptions } from '@glob2/engine/engine';

export type AiValidator = (bytes: Uint8Array, signal: AbortSignal, progress?: (report: AiValidationReport) => Promise<void>) => Promise<AiValidationReport>;
const fixtures = fileURLToPath(new URL('../fixtures/ais/', import.meta.url));
const sha = (b: Uint8Array) => createHash('sha256').update(b).digest('hex');
const LIMIT = 64 * 1024 * 1024;
export function checksumRecords(bytes: Buffer): Map<number, Buffer> {
  if (bytes.subarray(0, 4).toString() !== 'GCS1' || bytes.length < 20)
    throw Error('Invalid checksum trace');
  const teams = bytes.readUInt32LE(4),
    count = bytes.readUInt32LE(12),
    records = new Map<number, Buffer>();
  if (teams > 12 || count > 4096) throw Error('Invalid checksum dimensions');
  let p = 20;
  for (let i = 0; i < count; i++) {
    const start = p,
      tick = bytes.readUInt32LE(p);
    p += 8;
    for (let t = 0; t < teams; t++) {
      p += 4;
      for (let group = 0; group < 2; group++) {
        const n = bytes.readUInt32LE(p);
        p += 4;
        if (n > 65536) throw Error('Invalid entity count');
        for (let e = 0; e < n; e++) {
          const fields = bytes.readUInt32LE(p + 6);
          p += 10 + 4 * fields;
          if (p > bytes.length) throw Error('Truncated checksum trace');
        }
      }
    }
    if (records.has(tick)) throw Error('Duplicate checksum tick');
    records.set(tick, bytes.subarray(start, p));
  }
  if (p !== bytes.length || records.size === 0) throw Error('Incomplete checksum trace');
  return records;
}
export function sameContinuation(whole: Buffer, resumed: Buffer, boundary: number): boolean {
  const a = checksumRecords(whole),
    b = checksumRecords(resumed),
    expected = [...a.keys()].filter((t) => t >= boundary);
  return b.size === expected.length && expected.every((t) => !!b.get(t)?.equals(a.get(t)!));
}
async function bounded(path: string) {
  const s = await stat(path);
  if (!s.isFile() || s.size > LIMIT) throw Error('Validation output exceeds limit');
  return readFile(path);
}

export async function createAiValidator(
  options: EngineOptions,
  sim: SimVersion,
): Promise<AiValidator> {
  if (process.platform !== 'linux') throw Error('AI validation requires Linux namespace isolation');
  const bwrap = '/usr/bin/bwrap';
  await access(bwrap);
  const manifest = JSON.parse(await readFile(join(fixtures, 'manifest.json'), 'utf8')) as {
    file: string;
    sha256: string;
    players: number;
    seed: number;
  }[];
  for (const f of manifest)
    if (sha(await readFile(join(fixtures, f.file))) !== f.sha256)
      throw Error('AI validation fixture hash mismatch');
  const system: string[] = [];
  for (const dir of ['/usr', '/lib', '/lib64', '/opt/glob2/lib']) {
    try {
      await access(dir);
      system.push('--ro-bind', dir, dir);
    } catch {
      /* optional ABI library directory */
    }
  }
  const base = [
    ...system,
    '--unshare-all',
    '--die-with-parent',
    '--new-session',
    '--clearenv',
    '--proc',
    '/proc',
    '--dev',
    '/dev',
    '--tmpfs',
    '/tmp',
    '--dir',
    '/game',
    '--ro-bind',
    resolve(options.workdir, 'data'),
    '/game/data',
    '--ro-bind',
    options.binary,
    '/engine',
    '--ro-bind',
    fixtures,
    '/fixtures',
    '--chdir',
    '/game',
    '--setenv',
    'HOME',
    '/job',
    '--setenv',
    'GLOB2_USER_DIR',
    '/job/profile',
    '--setenv',
    'SDL_VIDEODRIVER',
    'dummy',
    '--setenv',
    'SDL_AUDIODRIVER',
    'dummy',
  ];
  async function run(scratch: string, args: string[], signal?: AbortSignal) {
    return runProcess({
      binary: bwrap,
      args: [...base, '--bind', scratch, '/job', '--', '/engine', ...args],
      cwd: options.workdir,
      limits: { timeoutMs: 120000, cpuSeconds: 120, memoryMb: 2048, fileSizeMb: 64 },
      maxCaptureBytes: 65536,
      ...(signal ? { signal } : {}),
    });
  }
  // Probe the actual engine inside isolation before advertising this job kind.
  await withScratchDir(options.scratchRoot, async (scratch) => {
    await writeFile(join(scratch, 'probe.js'), 'function step() {}');
    const p = await run(scratch, ['--check-ai-json', '/job/probe.js']);
    if (p.code !== 0 || !p.stdout.includes('"valid":true'))
      throw Error('Isolated AI validator probe failed: ' + p.stderr.slice(-500));
  });
  return async (bytes, signal, progress) =>
    withScratchDir(options.scratchRoot, async (scratch) => {
      const report = pendingAiReport(sha(bytes), simVersionKey(sim));
      let stage: (typeof AI_CHECKS)[number] = 'file';
      const pass = (id: typeof stage) => {
        report.checks.find((c) => c.id === id)!.status = 'passed';
      };
      try {
        if (bytes.length === 0 || bytes.length > 128 * 1024 || bytes.includes(0))
          throw Error('Expected a bundled JavaScript file up to 128 KiB without NUL bytes.');
        new TextDecoder('utf-8', { fatal: true }).decode(bytes);
        pass('file');
        await writeFile(join(scratch, 'source.js'), bytes);
        stage = 'syntax';
      report.checks.find(c => c.id === stage)!.status = 'running';
      await progress?.(report);
        const checked = await run(scratch, ['--check-ai-json', '/job/source.js'], signal);
        if (checked.timedOut || checked.signal)
          throw Error('Startup exceeded the validation resource limits.');
        const checkedJson = JSON.parse(checked.stdout) as {
          valid: boolean;
          failedCheck?: typeof stage;
          message?: string;
          metadata?: unknown;
        };
        if (!checkedJson.valid) {
          const failed = checkedJson.failedCheck;
          if (failed && ['syntax', 'startup', 'state'].includes(failed)) {
            for (const id of ['syntax', 'startup', 'state'] as const) {
              if (id === failed) break;
              pass(id);
            }
            stage = failed;
          }
          throw Error(checkedJson.message ?? 'AI startup failed');
        }
        report.metadata = parse(AiMetadata, checkedJson.metadata);
        pass('syntax');
        pass('startup');
        pass('state');
        stage = 'gameplay';
      report.checks.find(c => c.id === stage)!.status = 'running';
      await progress?.(report);
        async function game(name: string, args: string[]) {
          const dir = join(scratch, name);
          await mkdir(dir);
          const p = await run(
            scratch,
            [
              '--run-game',
              ...args,
              '--ticks',
              '4096',
              '--compute-threads',
              '1',
              '--telemetry',
              'checksums',
              '--save',
              'every:2048',
              '--save',
              'final',
              '--replay',
              'true',
              '--output-dir',
              '/job/' + name,
            ],
            signal,
          );
          if (p.code !== 0 || p.timedOut)
            throw Error(
              p.timedOut
                ? 'Gameplay exceeded the validation time limit.'
                : 'Gameplay failed: ' + p.stderr.slice(-1500),
            );
          const result = JSON.parse((await bounded(join(dir, 'result.json'))).toString()) as {
            status: string;
            ticks: number;
            termination: string;
            javascriptControllers?: { disabled: boolean; diagnostic: string }[];
          };
          if (result.status !== 'completed' || result.javascriptControllers?.length !== 1)
            throw Error('Missing controller health report');
          const failure = result.javascriptControllers.find((c) => c.disabled);
          if (failure) throw Error(failure.diagnostic || 'The JavaScript controller stopped.');
          if (result.ticks < 2048 && result.termination !== 'engine_end')
            throw Error('Game stopped before the checkpoint');
          return { trace: await bounded(join(dir, 'game.replay.checksums')), result };
        }
        const runs: { name: string; args: string[]; trace: Buffer; ticks: number }[] = [];
        for (const [index, f] of manifest.entries()) {
          const name = 'map' + index;
          // Save a current-format initial state before the measured run so save-format
          // header upgrades cannot contaminate continuation checksum comparisons.
          const args = [
            '--map-file',
            '/fixtures/' + f.file,
            '--game-seed',
            String(f.seed),
            '--player',
            'javascript',
            '--ai-script',
            '0:/job/source.js',
          ];
          for (let p = 1; p < f.players; p++) args.push('--player', 'numbi');
          const prepare = name + '-initial';
        await mkdir(join(scratch, prepare));
        const prepared = await run(scratch, ['--run-game', ...args, '--ticks', '1', '--save', 'initial', '--output-dir', '/job/' + prepare], signal);
        if (prepared.code !== 0) throw Error('Could not prepare validation fixture: ' + prepared.stderr.slice(-1500));
        const frozenArgs = ['--load-game', '/job/' + prepare + '/initial.game.gz'];
        const first = await game(name, frozenArgs);
          runs.push({ name, args: frozenArgs, trace: first.trace, ticks: first.result.ticks });
        }
        pass('gameplay');
        stage = 'determinism';
      report.checks.find(c => c.id === stage)!.status = 'running';
      await progress?.(report);
        for (const r of runs) {
          const again = await game(r.name + '-repeat', r.args);
          if (!r.trace.equals(again.trace))
            throw Error('Repeated seeded games produced different checksums.');
        }
        pass('determinism');
        stage = 'continuation';
      report.checks.find(c => c.id === stage)!.status = 'running';
      await progress?.(report);
        for (const r of runs) {
          if (r.ticks <= 2048) {
            report.checks.find((c) => c.id === 'continuation')!.message =
              'A fixture ended before its checkpoint; its final save was restored.';
            await game(r.name + '-resume', ['--load-game', '/job/' + r.name + '/final.game.gz']);
            continue;
          }
          const tail = await game(r.name + '-resume', [
            '--load-game',
            '/job/' + r.name + '/checkpoint-2048.game.gz',
          ]);
          if (!sameContinuation(r.trace, tail.trace, 2048))
            throw Error('Save/resume changed the simulation checksums.');
        }
        pass('continuation');
        report.valid = true;
      } catch (error) {
        if (signal.aborted) throw error;
        for (const c of report.checks) {
          if (c.id === stage) {
            c.status = 'failed';
            c.message = (error instanceof Error ? error.message : String(error)).slice(0, 2000);
          } else if (c.status === 'pending') c.status = 'skipped';
        }
      }
      report.suite = AI_VALIDATION_SUITE;
      return report;
    });
}
