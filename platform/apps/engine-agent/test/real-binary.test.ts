// generate → validate → preview against a real glob2 binary, for generators of
// the default quick-match pool. Runs only when GLOB2_BINARY names a binary;
// GLOB2_WORKDIR is the directory holding its data/ (default: the repository
// root). Example:
//   GLOB2_BINARY=build/darwin/client/release/src/glob2 npx vitest run apps/engine-agent
// GLOB2_EVIDENCE_DIR, when set, receives each preview PNG and the job results.
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { contentKey, defaultMapPool } from '@glob2/core';
import { engineJobs, schemaIssues, type EngineJob, type EngineJobKind } from '@glob2/protocol';
import { createRunner, SIM, type RunnerHarness } from './support.ts';

const REPO = fileURLToPath(new URL('../../../../', import.meta.url));
const binary = process.env['GLOB2_BINARY'] ? resolve(REPO, process.env['GLOB2_BINARY']) : undefined;
const workdir = resolve(REPO, process.env['GLOB2_WORKDIR'] ?? '.');
const GENERATORS = ['symmetric-arena', 'even-ground', 'marchland'];
const evidence = process.env['GLOB2_EVIDENCE_DIR'];

describe.runIf(binary)('real glob2 binary', () => {
  let database: TestDatabase;
  let h: RunnerHarness;
  const signal = new AbortController().signal;

  beforeAll(async () => {
    database = await createTestDatabase();
    h = await createRunner(database.db, { binary: binary!, workdir });
  });

  afterAll(async () => {
    await h?.close();
    await database?.drop();
  });

  async function run(kind: EngineJobKind, payload: unknown): Promise<Record<string, unknown>> {
    const job = { jobId: randomUUID(), kind, simVersion: SIM, payload } as EngineJob;
    const result = (await h.runner.run(job, signal)) as Record<string, unknown>;
    expect(schemaIssues(engineJobs[kind].result, result)).toEqual([]);
    return result;
  }

  for (const generatorId of GENERATORS) {
    it(`generates, validates and previews ${generatorId} from the default 1v1 pool`, async () => {
      const entry = defaultMapPool('1v1').find((e) => e.generatorId === generatorId)!;
      const generated = await run('generate-map', { generator: { ...entry, seed: 1234 } });
      expect(generated['map']).toEqual({ width: 128, height: 128, teamCount: 2 });
      const mapHash = generated['mapHash'] as string;

      // Same descriptor, same bytes: generation is deterministic on one platform.
      const again = await run('generate-map', { generator: { ...entry, seed: 1234 } });
      expect(again['mapHash']).toBe(mapHash);

      const validated = await run('validate-map', { blobHash: mapHash, format: 'map' });
      expect(validated).toMatchObject({
        valid: true,
        mapHash,
        map: { width: 128, height: 128, teamCount: 2 },
        versionMinor: (await h.engine.catalog()).versionMinor,
      });
      // A generated map is not a save.
      expect(await run('validate-map', { blobHash: mapHash, format: 'save' })).toEqual({
        valid: false,
        reason: 'file is a map, not a saved game',
      });

      const preview = await run('render-preview', { mapHash, maxSizePx: 256 });
      expect(preview).toMatchObject({ contentType: 'image/png', width: 256, height: 256 });
      if (evidence) {
        await mkdir(evidence, { recursive: true });
        const png = await h.store.get(contentKey(preview['previewHash'] as string));
        const chunks: Buffer[] = [];
        for await (const chunk of png!) chunks.push(chunk as Buffer);
        await writeFile(`${evidence}/${generatorId}.png`, Buffer.concat(chunks));
        await writeFile(
          `${evidence}/${generatorId}.json`,
          JSON.stringify({ generated, validated, preview }, null, 2),
        );
      }
      expect(await h.store.size(contentKey(preview['previewHash'] as string))).toBeGreaterThan(
        1000,
      );
    }, 120_000);
  }

  it('rejects a corrupt map with the engine loader', async () => {
    const bytes = Buffer.alloc(4096, 7);
    bytes.writeUInt32BE(4, 0);
    const { sha256 } = await import('@glob2/core').then((core) => core.putContent(h.store, bytes));
    const result = await run('validate-map', { blobHash: sha256, format: 'map' });
    expect(result['valid']).toBe(false);
  }, 60_000);
});
