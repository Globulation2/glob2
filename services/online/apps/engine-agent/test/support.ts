// Shared helpers for engine-agent tests: a runner wired to the fake binary
// (test/fake-glob2.mjs) or to a real glob2 binary, and fake map bytes.
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { FsBlobStore, contentKey, putContent } from '@glob2/core';
import type { SimVersion } from '@glob2/protocol';
import { checkBlob, type JobBlobs } from '../src/blobs.ts';
import { EngineInputError } from '../src/engineCli.ts';
import { DEFAULT_LIMITS, GlobEngine, type EngineOptions } from '../src/engine.ts';
import { HeadlessEngineRunner, type RunnerLimits } from '../src/runners.ts';

export const FAKE_BINARY = fileURLToPath(new URL('./fake-glob2.mjs', import.meta.url));
export const SIM: SimVersion = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };

/** Map bytes in the engine's header layout, with the fake binary's body (width, height). */
export function fakeMap(options: {
  name?: string;
  minor?: number;
  teams?: number;
  saved?: boolean;
  width?: number;
  height?: number;
}): Buffer {
  const {
    name = 'fake',
    minor = 125,
    teams = 2,
    saved = false,
    width = 128,
    height = 128,
  } = options;
  const nameBytes = Buffer.from(name, 'utf8');
  const buffer = Buffer.alloc(4 + nameBytes.length + 17 + 8 + 64);
  let o = 0;
  buffer.writeUInt32BE(nameBytes.length, o);
  o += 4;
  nameBytes.copy(buffer, o);
  o += nameBytes.length;
  buffer.writeInt32BE(0, o);
  buffer.writeInt32BE(minor, o + 4);
  buffer.writeInt32BE(teams, o + 8);
  buffer.writeUInt32BE(1234, o + 12);
  buffer.writeUInt8(saved ? 1 : 0, o + 16);
  o += 17;
  buffer.writeUInt32BE(width, o);
  buffer.writeUInt32BE(height, o + 4);
  return buffer;
}

/**
 * Blob access over a local store, as platform-api gives it to an agent:
 * stored blobs are registered with their content type and visibility.
 */
export class LocalBlobs implements JobBlobs {
  readonly registered = new Map<string, { contentType: string; visibility: string }>();
  readonly store: FsBlobStore;
  constructor(store: FsBlobStore) {
    this.store = store;
  }

  async read(sha256: string, maxBytes: number): Promise<Uint8Array> {
    const key = contentKey(sha256);
    const size = await this.store.size(key);
    if (size === undefined) throw new EngineInputError(`blob ${sha256} not found`);
    if (size > maxBytes)
      throw new EngineInputError(`blob ${sha256} is ${size} bytes; limit ${maxBytes}`);
    const stream = await this.store.get(key);
    const chunks: Buffer[] = [];
    for await (const chunk of stream!) chunks.push(chunk as Buffer);
    return checkBlob(sha256, Buffer.concat(chunks));
  }

  async write(
    bytes: Uint8Array,
    contentType: string,
    visibility: 'public' | 'private' = 'private',
  ): Promise<string> {
    const { sha256 } = await putContent(this.store, bytes);
    if (!this.registered.has(sha256)) this.registered.set(sha256, { contentType, visibility });
    return sha256;
  }
}

export interface RunnerHarness {
  runner: HeadlessEngineRunner;
  engine: GlobEngine;
  blobs: LocalBlobs;
  store: FsBlobStore;
  close(): Promise<void>;
}

export async function createRunner(
  options: {
    binary?: string;
    workdir?: string;
    simVersion?: SimVersion;
    engine?: Partial<EngineOptions>;
    limits?: Partial<RunnerLimits>;
  } = {},
): Promise<RunnerHarness> {
  const dir = await mkdtemp(join(tmpdir(), 'glob2-agent-test-'));
  const store = new FsBlobStore(join(dir, 'blobs'));
  const engine = new GlobEngine({
    binary: options.binary ?? FAKE_BINARY,
    workdir: options.workdir ?? dir,
    scratchRoot: dir,
    limits: DEFAULT_LIMITS,
    maxOutputBytes: 64 * 1024 * 1024,
    ...options.engine,
  });
  const catalog = await engine.catalog();
  const blobs = new LocalBlobs(store);
  const runner = new HeadlessEngineRunner({
    engine,
    catalog,
    simVersion: options.simVersion ?? SIM,
    blobs,
    ...(options.limits ? { limits: options.limits } : {}),
  });
  return {
    runner,
    engine,
    blobs,
    store,
    close: () => rm(dir, { recursive: true, force: true }),
  };
}
