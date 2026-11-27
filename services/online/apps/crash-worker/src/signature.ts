// Which frames are Glob2's own code, and the signature a crash is grouped by
// (docs/multiplayer/crash-reports.md#grouping).
import type { CrashFrame } from '@glob2/protocol';

/** Debug or code file names of the game's own executable on each platform. */
const GAME_MODULES = new Set([
  'glob2',
  'glob2.exe',
  'glob2.pdb',
  'libmain.so',
  'glob2.wasm',
  'globulation2',
  'globulation 2',
]);

/** Sources that are linked into the game but are not Glob2's code. */
const LIBRARY_FILE =
  /(third_party|vcpkg|_deps|sdl3?[-_/]|\/sdl\/|\/usr\/(include|lib|src)|include\/c\+\+|libstdc\+\+|libc\+\+|\/ndk\/|mingw|crt\/|emscripten\/system)/i;
const LIBRARY_FUNCTION =
  /^(SDL_|std::|__|_Unwind|abort\b|raise\b|operator new|operator delete|malloc\b|free\b|memcpy\b|memmove\b|memset\b|strlen\b)/;

export function isGameModule(module: string): boolean {
  const name = module.split(/[/\\]/).pop() ?? module;
  return GAME_MODULES.has(name.toLowerCase());
}

/**
 * A frame is in-game when it is in the game's module, has a function name and
 * comes from neither a bundled library's sources nor a runtime function.
 */
export function inGame(frame: Omit<CrashFrame, 'inGame'>): boolean {
  // Java frames (module "java"): the game's Android activity and its helpers.
  if (frame.module === 'java') return /^org\.globulation2\./.test(frame.function ?? '');
  if (!isGameModule(frame.module) || !frame.function) return false;
  if (frame.file && LIBRARY_FILE.test(frame.file)) return false;
  return !LIBRARY_FUNCTION.test(frame.function);
}

/** Drops compiler suffixes that differ between builds of the same function. */
export function normalizeFunction(name: string): string {
  return name
    .replace(/ \[clone [^\]]*\]/g, '')
    .replace(/\.(cold|part|isra|constprop|lto_priv)\.[0-9]+/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

/** Frames taken into a signature. */
export const SIGNATURE_FRAMES = 3;

/**
 * The top in-game frames' functions, innermost first, joined by " | ". Lines
 * and offsets are left out so a group survives releases. A stack without
 * in-game frames falls back to its top named frames, and a stack without any
 * symbols to its top frame's module and offset.
 */
export function signature(frames: readonly CrashFrame[]): string {
  const named = (list: readonly CrashFrame[]) =>
    list.flatMap((f) => (f.function ? [normalizeFunction(f.function)] : []));
  const own = named(frames.filter((f) => f.inGame)).slice(0, SIGNATURE_FRAMES);
  if (own.length) return own.join(' | ');
  const any = named(frames).slice(0, SIGNATURE_FRAMES);
  if (any.length) return any.join(' | ');
  const top = frames[0];
  return top ? `${top.module}+${top.offset}` : 'empty stack';
}

/** Compares release versions numerically (0.11.10 after 0.11.9). */
export function compareVersions(a: string, b: string): number {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (d) return d;
  }
  return 0;
}

/** Keeps the protocol's field limits. */
export function clampFrame(frame: CrashFrame): CrashFrame {
  const cut = (text: string | undefined, max: number) =>
    text === undefined ? undefined : text.length > max ? text.slice(0, max) : text;
  const out: CrashFrame = {
    module: cut(frame.module, 256) ?? '',
    offset: cut(frame.offset, 32) ?? '0x0',
    inGame: frame.inGame,
  };
  const fn = cut(frame.function, 1024);
  const file = cut(frame.file, 1024);
  if (fn) out.function = fn;
  if (file) out.file = file;
  if (frame.line !== undefined && Number.isInteger(frame.line) && frame.line >= 0)
    out.line = frame.line;
  return out;
}

/** Frames kept per report. */
export const MAX_FRAMES = 64;
