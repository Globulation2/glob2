import { randomUUID } from 'node:crypto';
import { expect, it, vi } from 'vitest';
import { buildingNamespacePrefix, type EngineJob } from '@glob2/protocol';
import { writeBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import { GlobEngine, DEFAULT_LIMITS, EngineCrashError } from '@glob2/engine/engine';
import { EngineInputError, EngineOutputError } from '@glob2/engine/engineCli';
import type { EngineCatalog } from '@glob2/engine/engineCli';
import { HeadlessEngineRunner } from '../src/runners.ts';
import type { JobBlobs } from '../src/blobs.ts';
const sim = { versionMinor: 144, netProtocol: 49, dataHash: 'aa'.repeat(32) };
const baseHash = 'bb'.repeat(32),
  namespace = randomUUID();
const pkg = {
  schemaVersion: 1,
  namespace,
  experiments: [],
  sprites: [],
  variants: [
    {
      key: buildingNamespacePrefix(namespace) + 'kitchen',
      properties: {
        width: 2,
        height: 2,
        hpInit: 200,
        hpMax: 200,
        gameSprite: 'data/gfx/inn0b',
        miniSprite: 'data/gfx/miniinn0b',
      },
      semantics: { placeable: true, instantPlacement: true },
    },
  ],
};
const archive = writeBuildingArchive(pkg, new Map());
const engine = new GlobEngine({
  binary: '/unused',
  workdir: '/unused',
  limits: DEFAULT_LIMITS,
  maxOutputBytes: 72 * 1024 * 1024,
});
const catalog = { commands: ['compose_buildings'] } as EngineCatalog;
const runner = new HeadlessEngineRunner({ engine, catalog, simVersion: sim });
const job: EngineJob = {
  jobId: randomUUID(),
  kind: 'validate-buildings',
  simVersion: sim,
  payload: { blobHash: buildingAssetHash(archive), baseHash, suite: 1 },
};
const blobs = { read: vi.fn(async () => archive) } as unknown as JobBlobs;
const signal = new AbortController().signal;
it('validates exact archive bytes against the requested stock catalog', async () => {
  const compose = vi.spyOn(engine, 'composeBuildings').mockResolvedValue({
    schemaVersion: 1,
    baseHash,
    catalog: { snapshot: '{}', hash: buildingAssetHash(Buffer.from('{}')) },
  });
  expect(runner.kinds).toContain('validate-buildings');
  const result = await runner.run(job, signal, blobs);
  expect(result).toMatchObject({
    valid: true,
    archiveHash: job.payload.blobHash,
    baseHash,
    suite: 1,
  });
  expect(compose).toHaveBeenCalledWith([pkg], signal, undefined);
  compose.mockResolvedValue({
    schemaVersion: 1,
    baseHash: 'cc'.repeat(32),
    catalog: { snapshot: '{}', hash: buildingAssetHash(Buffer.from('{}')) },
  });
  await expect(runner.run(job, signal, blobs)).rejects.toThrow('Stock catalog changed');
  compose.mockRestore();
});
it('only marks deterministic native input errors as invalid releases', async () => {
  const compose = vi
    .spyOn(engine, 'composeBuildings')
    .mockRejectedValue(new EngineInputError('Unresolved upgrade relationship'));
  expect(await runner.run(job, signal, blobs)).toMatchObject({
    valid: false,
    reason: 'Unresolved upgrade relationship',
  });
  compose.mockRejectedValue(new EngineOutputError('Malformed engine response'));
  await expect(runner.run(job, signal, blobs)).rejects.toThrow('Malformed engine response');
  const filesystem = new Error('Scratch directory is unavailable');
  compose.mockRejectedValue(filesystem);
  await expect(runner.run(job, signal, blobs)).rejects.toBe(filesystem);
  const abort = new AbortController();
  abort.abort();
  compose.mockRejectedValue(new EngineInputError('Command interrupted'));
  await expect(runner.run(job, abort.signal, blobs)).rejects.toThrow('Command interrupted');
  compose.mockRestore();
});
it('rejects invalid archives and preserves retryable process failures', async () => {
  const corrupt = Buffer.from(archive);
  corrupt[40] = (corrupt[40] ?? 0) ^ 1;
  await expect(
    runner.run(job, signal, { read: async () => corrupt } as unknown as JobBlobs),
  ).rejects.toThrow('Archive hash mismatch');
  const crash = new EngineCrashError('Engine crashed', {
    code: 1,
    stdout: '',
    stderr: '',
    signal: null,
  } as never);
  const compose = vi.spyOn(engine, 'composeBuildings').mockRejectedValue(crash);
  await expect(runner.run(job, signal, blobs)).rejects.toBe(crash);
  compose.mockRestore();
});
