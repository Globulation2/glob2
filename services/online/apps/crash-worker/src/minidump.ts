// Symbolicates a Crashpad minidump with rust-minidump's minidump-stackwalk
// (MIT; approved as a runtime dependency on 2026-11-26).
//
// The first pass reads the dump without symbols, to learn which modules it
// names; their symbols are fetched into the cache and the second pass unwinds
// with them (CFI included), so only the symbols a crash needs are read.
import { readFile, stat } from 'node:fs/promises';
import { join } from 'node:path';
import { runProcess, outputTail } from '@glob2/engine/process';
import type { CrashFrame } from '@glob2/protocol';
import type { SymbolCache } from './symbols.ts';
import { clampFrame, inGame, MAX_FRAMES } from './signature.ts';

export interface Stackwalker {
  binary: string;
  signal?: AbortSignal;
}

export interface Symbolicated {
  frames: CrashFrame[];
  reason: string;
  /** A group signature of its own, for reports without a stack (kind "error"). */
  signature?: string;
}

/** Thrown for input that will never symbolicate: the report is marked failed. */
export class UnreadableCrash extends Error {}

/** The fields of minidump-stackwalk's JSON this worker reads. */
interface StackwalkFrame {
  module?: string | null;
  module_offset?: string | null;
  offset?: string | null;
  function?: string | null;
  file?: string | null;
  line?: number | null;
  inlines?: { function?: string | null; file?: string | null; line?: number | null }[] | null;
}
interface StackwalkThread {
  frames?: StackwalkFrame[];
}
export interface StackwalkOutput {
  crash_info?: { type?: string | null; address?: string | null } | null;
  crashing_thread?: StackwalkThread | null;
  threads?: StackwalkThread[];
  modules?: { filename?: string; debug_file?: string | null; debug_id?: string | null }[];
}

const OUTPUT_LIMIT = 64 * 1024 * 1024;

async function walk(tool: Stackwalker, dump: string, output: string, symbols?: string) {
  const result = await runProcess({
    binary: tool.binary,
    cwd: '/',
    args: [
      '--json',
      '--no-interactive',
      '--output-file',
      output,
      ...(symbols ? ['--symbols-path', symbols] : []),
      dump,
    ],
    ...(tool.signal ? { signal: tool.signal } : {}),
    limits: { timeoutMs: 120_000, cpuSeconds: 100, memoryMb: 4096, fileSizeMb: 64 },
  });
  if (result.timedOut) throw new Error('minidump-stackwalk timed out');
  if (result.code !== 0)
    throw new UnreadableCrash(`minidump-stackwalk failed: ${outputTail(result, 300)}`);
  const info = await stat(output);
  if (info.size > OUTPUT_LIMIT) throw new UnreadableCrash('minidump-stackwalk output too large');
  return JSON.parse(await readFile(output, 'utf8')) as StackwalkOutput;
}

/** The crashing thread's frames (inlined calls first), innermost first. */
export function framesOf(output: StackwalkOutput): Symbolicated {
  const thread = output.crashing_thread ?? output.threads?.[0];
  const frames: CrashFrame[] = [];
  const add = (
    module: string,
    offset: string,
    fn?: string | null,
    file?: string | null,
    line?: number | null,
  ) => {
    const frame = {
      module,
      offset,
      ...(fn ? { function: fn } : {}),
      ...(file ? { file } : {}),
      ...(typeof line === 'number' ? { line } : {}),
    };
    frames.push(clampFrame({ ...frame, inGame: inGame(frame) }));
  };
  for (const frame of thread?.frames ?? []) {
    const module = frame.module ?? '';
    const offset = frame.module_offset ?? frame.offset ?? '0x0';
    for (const inline of frame.inlines ?? [])
      add(module, offset, inline.function, inline.file, inline.line);
    add(module, offset, frame.function, frame.file, frame.line);
    if (frames.length >= MAX_FRAMES) break;
  }
  return {
    frames: frames.slice(0, MAX_FRAMES),
    reason: output.crash_info?.type ?? 'unknown',
  };
}

export async function symbolicateMinidump(
  tool: Stackwalker,
  cache: SymbolCache,
  dump: string,
  work: string,
): Promise<Symbolicated> {
  const first = await walk(tool, dump, join(work, 'modules.json'));
  let found = 0;
  for (const module of first.modules ?? []) {
    // MinGW builds name no PDB: minidump-stackwalk then looks the module up by its code file.
    const file = module.debug_file || module.filename;
    if (!file || !module.debug_id) continue;
    const name = file.split(/[/\\]/).pop() ?? '';
    if (await cache.file('breakpad', name, module.debug_id)) found++;
  }
  const output = found ? await walk(tool, dump, join(work, 'stack.json'), cache.root) : first;
  return framesOf(output);
}
