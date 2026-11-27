// Symbolicates a browser error (web-error.json, protocol WebErrorReport). Its
// stack names WebAssembly frames by function index (wasm-function[N]:0xOFF in
// Chrome, Firefox and Safari alike); the Emscripten symbol map uploaded for
// the build ID turns indices into function names.
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';
import { WebErrorReport, type CrashFrame } from '@glob2/protocol';
import { Value } from 'typebox/value';
import type { SymbolCache } from './symbols.ts';
import { UnreadableCrash, type Symbolicated } from './minidump.ts';
import { clampFrame, inGame, MAX_FRAMES } from './signature.ts';

const WASM_FRAME = /wasm-function\[([0-9]{1,9})\]:(0x[0-9a-f]{1,16})/gi;

/** Function indices and byte offsets of a stack's wasm frames, innermost first. */
export function wasmFrames(stack: string): { index: number; offset: string }[] {
  const frames: { index: number; offset: string }[] = [];
  for (const match of stack.matchAll(WASM_FRAME)) {
    frames.push({ index: Number(match[1]), offset: (match[2] ?? '0x0').toLowerCase() });
    if (frames.length >= MAX_FRAMES) break;
  }
  return frames;
}

/** Reads the names of `indices` from an Emscripten symbol map (index:name per line). */
export async function symbolMapNames(path: string, indices: ReadonlySet<number>) {
  const names = new Map<number, string>();
  const lines = createInterface({ input: createReadStream(path), crlfDelay: Infinity });
  for await (const line of lines) {
    const colon = line.indexOf(':');
    if (colon <= 0) continue;
    const index = Number(line.slice(0, colon));
    if (indices.has(index)) names.set(index, line.slice(colon + 1));
    if (names.size === indices.size) break;
  }
  return names;
}

export async function symbolicateWeb(cache: SymbolCache, text: string): Promise<Symbolicated> {
  let report: WebErrorReport;
  try {
    const parsed: unknown = JSON.parse(text);
    if (!Value.Check(WebErrorReport, parsed)) throw new Error('schema');
    report = parsed;
  } catch {
    throw new UnreadableCrash('web-error.json is not a WebErrorReport');
  }
  const raw = wasmFrames(report.stack);
  const path = await cache.file('wasm', 'glob2.wasm', report.buildId);
  const names = path ? await symbolMapNames(path, new Set(raw.map((f) => f.index))) : new Map();
  const frames: CrashFrame[] = raw.map((f) => {
    const name = names.get(f.index) as string | undefined;
    const frame = { module: 'glob2.wasm', offset: f.offset, ...(name ? { function: name } : {}) };
    return clampFrame({ ...frame, inGame: inGame(frame) });
  });
  const reason = report.message.split('\n')[0]?.slice(0, 200) || report.source;
  return { frames, reason };
}
