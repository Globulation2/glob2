// Shared helpers for engine-agent tests: a runner wired to the fake binary
// (test/fake-glob2.mjs) or to a real glob2 binary, and fake map bytes.
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import type { Kysely } from 'kysely';
import { FsBlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import type { SimVersion } from '@glob2/protocol';
import { AgentBlobs } from '../src/blobs.ts';
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

export interface RunnerHarness {
  runner: HeadlessEngineRunner;
  engine: GlobEngine;
  blobs: AgentBlobs;
  store: FsBlobStore;
  close(): Promise<void>;
}

export async function createRunner(
  db: Kysely<Database>,
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
  const blobs = new AgentBlobs(store, db);
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
