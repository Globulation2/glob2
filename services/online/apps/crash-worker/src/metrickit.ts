// Symbolicates an iOS MetricKit crash diagnostic: the JSON that
// MXCrashDiagnostic.jsonRepresentation() writes, forwarded unchanged by the
// game as metrickit.json. Its frames are binary UUIDs and offsets into each
// binary's __TEXT segment, which are Breakpad addresses of the dSYM's symbols.
import type { CrashFrame } from '@glob2/protocol';
import { lookup } from './breakpad.ts';
import type { SymbolCache } from './symbols.ts';
import { UnreadableCrash, type Symbolicated } from './minidump.ts';
import { clampFrame, inGame, MAX_FRAMES } from './signature.ts';

interface MetricKitFrame {
  binaryUUID?: unknown;
  binaryName?: unknown;
  offsetIntoBinaryTextSegment?: unknown;
  subFrames?: unknown;
}

const SIGNALS: Record<number, string> = {
  4: 'SIGILL',
  5: 'SIGTRAP',
  6: 'SIGABRT',
  7: 'SIGEMT',
  8: 'SIGFPE',
  9: 'SIGKILL',
  10: 'SIGBUS',
  11: 'SIGSEGV',
  12: 'SIGSYS',
};
const EXCEPTIONS: Record<number, string> = {
  1: 'EXC_BAD_ACCESS',
  2: 'EXC_BAD_INSTRUCTION',
  3: 'EXC_ARITHMETIC',
  4: 'EXC_EMULATION',
  5: 'EXC_SOFTWARE',
  6: 'EXC_BREAKPOINT',
  10: 'EXC_CRASH',
  11: 'EXC_RESOURCE',
  12: 'EXC_GUARD',
};

/** The Breakpad debug ID of a Mach-O UUID (dashes dropped, age 0 appended). */
export function machoDebugId(uuid: string): string | undefined {
  const hex = uuid.replace(/-/g, '').toUpperCase();
  return /^[0-9A-F]{32}$/.test(hex) ? `${hex}0` : undefined;
}

function record(value: unknown): Record<string, unknown> | undefined {
  return value && typeof value === 'object' && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : undefined;
}

interface RawFrame {
  binary: string;
  uuid: string | undefined;
  offset: number;
}

/** The attributed thread's frames, innermost first. */
export function parseMetricKit(text: string): { frames: RawFrame[]; reason: string } {
  let json: unknown;
  try {
    json = JSON.parse(text);
  } catch {
    throw new UnreadableCrash('metrickit.json is not JSON');
  }
  const root = record(json);
  const meta = record(root?.['diagnosticMetaData']) ?? {};
  const stacks = record(root?.['callStackTree'])?.['callStacks'];
  if (!Array.isArray(stacks)) throw new UnreadableCrash('metrickit.json has no call stacks');
  const thread =
    stacks.map(record).find((s) => s?.['threadAttributed'] === true) ?? record(stacks[0]);
  const frames: RawFrame[] = [];
  // Each frame's subFrames holds its caller; the root frame is the innermost.
  let level: unknown = thread?.['callStackRootFrames'];
  while (Array.isArray(level) && level.length && frames.length < MAX_FRAMES) {
    const frame = record(level[0]) as MetricKitFrame | undefined;
    if (!frame) break;
    const offset = frame.offsetIntoBinaryTextSegment;
    if (typeof offset === 'number' && Number.isSafeInteger(offset) && offset >= 0)
      frames.push({
        binary: typeof frame.binaryName === 'string' ? frame.binaryName : '',
        uuid: typeof frame.binaryUUID === 'string' ? machoDebugId(frame.binaryUUID) : undefined,
        offset,
      });
    level = frame.subFrames;
  }
  const exception =
    typeof meta['exceptionType'] === 'number' ? EXCEPTIONS[meta['exceptionType']] : undefined;
  const signal = typeof meta['signal'] === 'number' ? SIGNALS[meta['signal']] : undefined;
  const reason = [exception, signal].filter(Boolean).join(' / ') || 'unknown';
  return { frames, reason };
}

export async function symbolicateMetricKit(
  cache: SymbolCache,
  text: string,
): Promise<Symbolicated> {
  const parsed = parseMetricKit(text);
  const byModule = new Map<string, RawFrame[]>();
  for (const frame of parsed.frames) {
    if (!frame.uuid || !frame.binary) continue;
    const key = `${frame.binary}\n${frame.uuid}`;
    byModule.set(key, [...(byModule.get(key) ?? []), frame]);
  }
  const hits = new Map<RawFrame, { function?: string; file?: string; line?: number }>();
  for (const [key, frames] of byModule) {
    const [binary = '', uuid = ''] = key.split('\n');
    const path = await cache.file('breakpad', binary, uuid);
    if (!path) continue;
    const found = await lookup(
      path,
      frames.map((f) => f.offset),
    );
    for (const frame of frames) {
      const hit = found.get(frame.offset);
      if (hit) hits.set(frame, hit);
    }
  }
  const frames: CrashFrame[] = parsed.frames.map((raw) => {
    const hit = hits.get(raw) ?? {};
    const frame = {
      module: raw.binary || 'unknown',
      offset: `0x${raw.offset.toString(16)}`,
      ...hit,
    };
    return clampFrame({ ...frame, inGame: inGame(frame) });
  });
  return { frames, reason: parsed.reason };
}
