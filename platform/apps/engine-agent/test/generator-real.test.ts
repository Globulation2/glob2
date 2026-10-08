import { REAL_GENERATOR_PACKAGE as source } from './generatorFixture.ts';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { createGeneratorExecutor } from '../src/generatorValidation.ts';
import { DEFAULT_LIMITS } from '../src/engine.ts';
const binary = process.env.GLOB2_BINARY,
  scratch = process.env.GLOB2_GENERATOR_SCRATCH;
const workdir = resolve(import.meta.dirname, '../../../..');
describe.runIf(binary && scratch)('real isolated generator packages', () => {
  it(
    'probes, canonicalizes, samples, reloads and generates the exact package',
    async () => {
      const { stdout } = await promisify(execFile)(resolve(workdir, binary!), ['--sim-version'], {
        cwd: workdir,
      });
      const sim = JSON.parse(stdout);
      const executor = await createGeneratorExecutor(
        {
          binary: resolve(workdir, binary!),
          workdir: process.env.GLOB2_GENERATOR_WORKDIR ?? workdir,
          scratchRoot: scratch!,
          limits: DEFAULT_LIMITS,
          maxOutputBytes: 64 * 1024 * 1024,
        },
        sim,
      );
      const settings = {
        seed: 19,
        params: { width: 7, height: 7, teams: 2, workers: 4 },
        candidates: 1,
        startingUnitLevel: 0 as const,
      };
      const result = await executor.validate(source, settings, new AbortController().signal);
      const output = resolve(workdir, 'artifacts/generator-library/isolated');
      await mkdir(output, { recursive: true });
      await writeFile(resolve(output, 'validation.json'), JSON.stringify(result.report, null, 2));
      expect(result.report.valid, result.report.error).toBe(true);
      expect(result.canonical).toBeDefined();
      expect(result.report.metadata?.controls).toMatchObject([
        {
          id: 'roughness',
          kind: 'range',
          group: 'terrain',
          minimum: 0,
          maximum: 2,
          default: 1,
          values: [0, 1, 2],
        },
        {
          id: 'season',
          kind: 'choice',
          group: 'resources',
          choices: ['Summer', 'Winter'],
          values: [0, 1],
        },
      ]);
      expect(result.report.samples.some((s) => s.status === 'refused')).toBe(true);
      expect(createHash('sha256').update(result.canonical!).digest('hex')).toBe(
        result.report.fileHash,
      );
      await writeFile(resolve(output, 'canonical.json'), result.canonical!);
      await writeFile(resolve(output, 'preview.png'), result.png!);
      const generated = await executor.generate(
        result.canonical!,
        {
          ...settings,
          libraryId: '11111111-1111-4111-8111-111111111111',
          versionId: '22222222-2222-4222-8222-222222222222',
          fileHash: result.report.fileHash!,
          packageHash: result.report.packageHash!,
          generatorId: 'test:shared-generator',
          revision: 1,
        },
        new AbortController().signal,
      );
      expect(generated.map.teamCount).toBe(2);
      expect(generated.chosenSeed).toBeGreaterThanOrEqual(0);
      expect(generated.chosenSeed).toBeLessThanOrEqual(0xffffffff);
      await writeFile(resolve(output, 'map.map'), generated.bytes);
      const broken = JSON.parse(source.toString());
      broken.modules['generator.js'] = "import './absent.js';export function generate(c){}";
      expect(
        (
          await executor.validate(
            Buffer.from(JSON.stringify(broken)),
            settings,
            new AbortController().signal,
          )
        ).report.valid,
      ).toBe(false);
      broken.modules['generator.js'] = 'export function generate(c){while(true){}}';
      const runaway = await executor.validate(
        Buffer.from(JSON.stringify(broken)),
        settings,
        new AbortController().signal,
      );
      expect(runaway.report.valid).toBe(false);
      expect(runaway.report.error).toMatch(/budget|limit|failed/i);
      await writeFile(
        resolve(output, 'runaway-validation.json'),
        JSON.stringify(runaway.report, null, 2),
      );
    },
    15 * 60 * 1000,
  );
});
