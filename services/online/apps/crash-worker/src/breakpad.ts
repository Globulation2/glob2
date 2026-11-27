// Looks up module offsets in a Breakpad .sym file: the function (FUNC, else the
// nearest PUBLIC symbol) and source line of each. minidump-stackwalk does this
// itself for minidumps; MetricKit stacks arrive as binary offsets and use this.
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';

export interface SymbolHit {
  function?: string;
  file?: string;
  line?: number;
}

interface Range {
  start: number;
  end: number;
}

/** Index of the last sorted address <= value, or -1. */
function floor(sorted: readonly number[], value: number): number {
  let lo = 0;
  let hi = sorted.length - 1;
  let found = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if ((sorted[mid] ?? 0) <= value) {
      found = mid;
      lo = mid + 1;
    } else hi = mid - 1;
  }
  return found;
}

/** The addresses of `wanted` inside [start, end). */
function within(sorted: readonly number[], range: Range): number[] {
  const out: number[] = [];
  for (let i = Math.max(0, floor(sorted, range.start)); i < sorted.length; i++) {
    const address = sorted[i] ?? 0;
    if (address >= range.end) break;
    if (address >= range.start) out.push(address);
  }
  return out;
}

/**
 * Reads `path` once and resolves every offset in `offsets`. Lines are streamed,
 * so a symbol file of hundreds of megabytes costs no more memory than its FILE
 * table.
 */
export async function lookup(
  path: string,
  offsets: readonly number[],
): Promise<Map<number, SymbolHit>> {
  const wanted = [...new Set(offsets)].sort((a, b) => a - b);
  const hits = new Map<number, SymbolHit>();
  const files = new Map<number, string>();
  const publics: { address: number; name: string }[] = [];
  let current: { range: Range; name: string; inside: number[] } | undefined;
  const lines = createInterface({ input: createReadStream(path), crlfDelay: Infinity });
  for await (const line of lines) {
    const first = line.charCodeAt(0);
    // Records start with an uppercase keyword; line records with a lowercase hex address.
    if ((first >= 48 && first <= 57) || (first >= 97 && first <= 102)) {
      // A line record of the current FUNC: address size line file.
      if (!current?.inside.length) continue;
      const [a, s, l, f] = line.split(' ');
      const range = { start: parseInt(a ?? '', 16), end: 0 };
      range.end = range.start + parseInt(s ?? '', 16);
      for (const address of within(current.inside, range)) {
        const hit = hits.get(address);
        if (hit && hit.line === undefined) {
          hit.line = Number(l);
          const file = files.get(Number(f));
          if (file !== undefined) hit.file = file;
        }
      }
      continue;
    }
    current = undefined;
    if (line.startsWith('FILE ')) {
      const space = line.indexOf(' ', 5);
      files.set(Number(line.slice(5, space)), line.slice(space + 1));
    } else if (line.startsWith('FUNC ')) {
      const parts = line.split(' ');
      const offset = parts[1] === 'm' ? 1 : 0;
      const start = parseInt(parts[1 + offset] ?? '', 16);
      const size = parseInt(parts[2 + offset] ?? '', 16);
      const name = parts.slice(4 + offset).join(' ');
      const range = { start, end: start + size };
      const inside = within(wanted, range);
      for (const address of inside) hits.set(address, { function: name });
      current = { range, name, inside };
    } else if (line.startsWith('PUBLIC ')) {
      const parts = line.split(' ');
      const offset = parts[1] === 'm' ? 1 : 0;
      publics.push({
        address: parseInt(parts[1 + offset] ?? '', 16),
        name: parts.slice(3 + offset).join(' '),
      });
    }
  }
  // Offsets outside every FUNC take the nearest PUBLIC symbol below them.
  publics.sort((a, b) => a.address - b.address);
  const starts = publics.map((p) => p.address);
  for (const address of wanted) {
    if (hits.has(address)) continue;
    const index = floor(starts, address);
    const symbol = publics[index];
    if (symbol) hits.set(address, { function: symbol.name });
  }
  return hits;
}
