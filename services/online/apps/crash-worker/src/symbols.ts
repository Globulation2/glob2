// Uploaded symbol files, decompressed on the worker's disk on first use and
// laid out the way minidump-stackwalk looks for Breakpad symbols:
// <root>/<debug file>/<debug id>/<debug file without .pdb>.sym. The cache keeps
// at most `maxBytes`, dropping the least recently used files first.
import { createWriteStream } from 'node:fs';
import { mkdir, readdir, rename, rm, stat, utimes } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { pipeline } from 'node:stream/promises';
import { createGunzip } from 'node:zlib';
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { BlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import type { CrashSymbolKind } from '@glob2/protocol';

/** A module name safe to use as one path component. */
export function safeModule(module: string): boolean {
  return (
    module.length > 0 && module.length <= 128 && !/[/\\\0]/.test(module) && !/^\.+$/.test(module)
  );
}

/** Where minidump-stackwalk looks for a module's Breakpad symbols under `root`. */
export function breakpadPath(root: string, module: string, debugId: string): string {
  const stem = module.replace(/\.pdb$/i, '');
  return join(root, module, debugId, `${stem}.sym`);
}

export class SymbolCache {
  readonly root: string;
  private readonly db: Kysely<Database>;
  private readonly blobs: BlobStore;
  private readonly maxBytes: number;

  constructor(root: string, db: Kysely<Database>, blobs: BlobStore, maxBytes: number) {
    this.root = root;
    this.db = db;
    this.blobs = blobs;
    this.maxBytes = maxBytes;
  }

  /**
   * The path of a module's symbols on disk, fetching and decompressing them
   * when they are not cached yet; undefined when none were uploaded.
   */
  async file(kind: CrashSymbolKind, module: string, debugId: string): Promise<string | undefined> {
    if (!safeModule(module) || !/^[0-9A-Fa-f]{32,40}$/.test(debugId)) return undefined;
    const path =
      kind === 'breakpad'
        ? breakpadPath(this.root, module, debugId)
        : join(this.root, 'wasm', `${debugId.toLowerCase()}.symbols`);
    const now = new Date();
    try {
      await utimes(path, now, now);
      return path;
    } catch {
      // Not cached yet.
    }
    const row = await this.db
      .selectFrom('crash_symbols')
      .select('blob_key')
      .where('kind', '=', kind)
      .where('module', '=', module)
      .where('debug_id', '=', debugId)
      .executeTakeFirst();
    const stream = row ? await this.blobs.get(row.blob_key) : undefined;
    if (!stream) return undefined;
    await mkdir(dirname(path), { recursive: true });
    const partial = `${path}.${randomUUID()}.partial`;
    try {
      await pipeline(stream, createGunzip(), createWriteStream(partial));
      await rename(partial, path);
    } finally {
      await rm(partial, { force: true });
    }
    return path;
  }

  /** Drops the least recently used symbol files until the cache fits. */
  async evict(): Promise<void> {
    const files: { path: string; size: number; used: number }[] = [];
    const walk = async (dir: string) => {
      let entries;
      try {
        entries = await readdir(dir, { withFileTypes: true });
      } catch {
        return;
      }
      for (const entry of entries) {
        const path = join(dir, entry.name);
        if (entry.isDirectory()) await walk(path);
        else if (entry.isFile() && !entry.name.endsWith('.partial')) {
          const info = await stat(path);
          files.push({ path, size: info.size, used: info.mtimeMs });
        }
      }
    };
    await walk(this.root);
    let total = files.reduce((sum, f) => sum + f.size, 0);
    files.sort((a, b) => a.used - b.used);
    for (const file of files) {
      if (total <= this.maxBytes) break;
      await rm(file.path, { force: true });
      total -= file.size;
    }
  }
}
