// generate → validate → preview against a real glob2 binary, for generators of
// the default quick-match pool. Runs only when GLOB2_BINARY names a binary;
// GLOB2_WORKDIR is the directory holding its data/ (default: the repository
// root). Example:
//   GLOB2_BINARY=build/darwin/client/release/src/glob2 npx vitest run apps/engine-agent
// GLOB2_EVIDENCE_DIR, when set, receives each preview PNG and the job results.
import { execFile } from 'node:child_process';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { promisify } from 'node:util';
import { gunzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { contentKey, defaultMapPool } from '@glob2/core';
import { engineJobs, schemaIssues, type EngineJob, type EngineJobKind } from '@glob2/protocol';
import { createRunner, SIM, type RunnerHarness } from './support.ts';

const REPO = fileURLToPath(new URL('../../../../', import.meta.url));
const binary = process.env['GLOB2_BINARY'] ? resolve(REPO, process.env['GLOB2_BINARY']) : undefined;
const workdir = resolve(REPO, process.env['GLOB2_WORKDIR'] ?? '.');
const GENERATORS = ['symmetric-arena', 'even-ground', 'marchland'];
const evidence = process.env['GLOB2_EVIDENCE_DIR'];

describe.runIf(binary)('real glob2 binary', () => {
  let h: RunnerHarness;
  const signal = new AbortController().signal;

  beforeAll(async () => {
    h = await createRunner({ binary: binary!, workdir });
  });

  afterAll(async () => {
    await h?.close();
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
      expect(generated['map']).toMatchObject({
        width: 128,
        height: 128,
        teamCount: 2,
        buildingCatalog: {
          hash: expect.stringMatching(/^[0-9a-f]{64}$/),
          snapshot: expect.any(String),
        },
        resourceExperiments: expect.arrayContaining([
          expect.objectContaining({ key: 'foundation-resources' }),
        ]),
        requiredResourceExperiments: [],
      });
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
      // Embedded catalog and resource experiment metadata survive validation.
      expect(validated['map']).toEqual(generated['map']);
      // A generated map is not a save.
      expect(await run('validate-map', { blobHash: mapHash, format: 'save' })).toEqual({
        valid: false,
        reason: 'This file is a map, not a saved game.',
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

  it('validates a saved game and lists its players', async () => {
    // A short headless game on a repository map, saved at its end.
    const out = await mkdtemp(join(tmpdir(), 'glob2-save-'));
    try {
      await promisify(execFile)(
        binary!,
        [
          '--run-game',
          '--map-file',
          resolve(REPO, 'maps/balanced_for_2.map.gz'),
          '--game-seed',
          '2',
          '--player',
          'nicowar',
          '--player',
          'cortex',
          '--ticks',
          '20',
          '--save',
          'final',
          '--output-dir',
          join(out, 'game'),
        ],
        { cwd: workdir, env: { ...process.env, SDL_VIDEODRIVER: 'dummy' }, timeout: 60_000 },
      );
      const save = gunzipSync(await readFile(join(out, 'game', 'final.game.gz')));
      const { sha256 } = await import('@glob2/core').then((core) => core.putContent(h.store, save));
      const result = await run('validate-map', { blobHash: sha256, format: 'save' });
      expect(result).toMatchObject({ valid: true, mapHash: sha256, map: { teamCount: 2 } });
      // Names need an engine whose map report includes them; older ones get
      // slot-numbered names.
      const players = result['players'] as { name: string; team: number; kind: string }[];
      expect(players.map((p) => [p.team, p.kind])).toEqual([
        [0, 'ai'],
        [1, 'ai'],
      ]);
      expect(['nicowar', 'AI 1']).toContain(players[0]!.name);
      if (evidence) {
        await mkdir(evidence, { recursive: true });
        await writeFile(`${evidence}/save-validation.json`, JSON.stringify(result, null, 2));
      }
    } finally {
      await rm(out, { recursive: true, force: true });
    }
  }, 120_000);

  it('rejects a corrupt map with the engine loader', async () => {
    const bytes = Buffer.alloc(4096, 7);
    bytes.writeUInt32BE(4, 0);
    const { sha256 } = await import('@glob2/core').then((core) => core.putContent(h.store, bytes));
    const result = await run('validate-map', { blobHash: sha256, format: 'map' });
    expect(result['valid']).toBe(false);
  }, 60_000);
});
