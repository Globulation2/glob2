import { createHash } from 'node:crypto';
import { readFile, writeFile } from 'node:fs/promises';
import type * as Fs from 'node:fs/promises';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';
import { beforeEach, expect, it, vi } from 'vitest';
import type * as Process from '@glob2/engine/process';
import { runProcess } from '@glob2/engine/process';
import { DEFAULT_LIMITS } from '../src/engine.ts';
import { createGeneratorExecutor, generatorMatrix } from '../src/generatorValidation.ts';
import { SIM } from './support.ts';
vi.mock('node:fs/promises', async (original) => {
  const fs = await original<typeof Fs>();
  return {
    ...fs,
    access: async (path: string) => {
      if (path !== '/usr/bin/bwrap') await fs.access(path);
    },
    statfs: async () => ({ type: 0x01021994, blocks: 1024, bsize: 4096 }),
  };
});
vi.mock('@glob2/engine/process', async (original) => ({
  ...(await original<typeof Process>()),
  runProcess: vi.fn(),
}));
const example = {
  seed: 19,
  params: { width: 7, height: 7, teams: 4, workers: 4 },
  candidates: 1,
  startingUnitLevel: 0 as const,
};
const source = Buffer.from(
  JSON.stringify({
    formatVersion: 1,
    manifest: {
      id: 'author:test',
      name: 'Test',
      apiVersion: 1,
      revision: 1,
      editorOnly: false,
      controls: [
        {
          id: 'water',
          label: 'Water',
          group: 'terrain',
          kind: 'range',
          minimum: 0,
          maximum: 10,
          step: 1,
          default: 5,
        },
      ],
      tags: [],
    },
    modules: { 'generator.js': 'export function generate(c){}' },
  }),
);
let failure = '',
  repeatable = true;
beforeEach(() => {
  failure = '';
  repeatable = true;
  vi.mocked(runProcess).mockReset();
});
async function executor() {
  vi.mocked(runProcess).mockImplementation(async (options) => {
    expect(options.binary).toBe('/usr/bin/bwrap');
    expect(options.args).toContain('--unshare-all');
    expect(options.args).toContain('--clearenv');
    expect(options.limits?.memoryMb).toBe(2048);
    const args = options.args,
      scratch = args[args.indexOf('--bind') + 1]!;
    const path = (arg: string) => join(scratch, arg.slice('/job/'.length));
    let code = 0,
      stderr = '';
    if (args.includes('--inspect-generator-package')) {
      const bytes = await readFile(path(args[args.indexOf('--inspect-generator-package') + 1]!));
      const manifest = JSON.parse(bytes.toString()).manifest;
      if (manifest.id === 'author:test' && failure === 'import') {
        code = 1;
        stderr = 'Invalid import';
      } else {
        await writeFile(path(args[args.indexOf('--output') + 1]!), bytes);
        await writeFile(
          path(args[args.indexOf('--json') + 1]!),
          JSON.stringify({
            ...manifest,
            description: '',
            toolkitVersion: 1,
            packageHash: createHash('sha256').update(bytes).digest('hex'),
          }),
        );
      }
    } else if (args.includes('--generate-map')) {
      const out = path(args[args.indexOf('--output-dir') + 1]!);
      const probe = out.endsWith('/probe');
      if (!probe && failure === 'isolation')
        return {
          code: 1,
          signal: null,
          timedOut: false,
          ms: 1,
          stdout: '',
          stderr: 'bwrap: namespace failed',
        };
      if (!probe && failure === 'timeout')
        return { code: 1, signal: null, timedOut: true, ms: 120000, stdout: '', stderr: '' };
      const seed = Number(args[args.indexOf('--map-seed') + 1]);
      const refused = !probe && seed === 20 && failure === 'refusal';
      const invalid = !probe && failure === 'invalid_world';
      await writeFile(
        join(out, 'result.json'),
        JSON.stringify({
          status: refused || invalid ? 'generation_failed' : 'completed',
          map_report: {
            generation: {
              outcome: { error: refused ? 'invalid_request' : invalid ? 'invalid_world' : 'none' },
            },
          },
        }),
      );
      await writeFile(
        join(out, 'map-r0.map.gz'),
        gzipSync(Buffer.from(!repeatable && out.includes('repeat-') ? 'different' : 'world')),
      );
    } else if (args.includes('--preview-map')) {
      await writeFile(path(args[args.indexOf('--output') + 1]!), Buffer.from('png'));
    }
    return { code, signal: null, timedOut: false, ms: 1, stdout: '', stderr };
  });
  return createGeneratorExecutor(
    {
      binary: '/fake',
      workdir: process.cwd(),
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 64 * 1024 * 1024,
    },
    SIM,
  );
}
it('isolates inspection, every repeated matrix run and saved-world inspection', async () => {
  const e = await executor();
  const r = await e.validate(source, example, new AbortController().signal);
  expect(r.report.valid).toBe(true);
  expect(r.report.samples).toHaveLength(10);
  expect(r.canonical).toEqual(source);
  expect(r.report.samples.every((s) => !!s.fingerprint)).toBe(true);
  expect(
    generatorMatrix(example, r.report.metadata!).some((s) => s.params.width !== s.params.height),
  ).toBe(true);
});
it('shows legitimate refusals but rejects invalid worlds and non-repeatable accepted requests', async () => {
  const e = await executor();
  failure = 'refusal';
  expect((await e.validate(source, example, new AbortController().signal)).report).toMatchObject({
    valid: true,
    samples: expect.arrayContaining([expect.objectContaining({ status: 'refused' })]),
  });
  failure = 'invalid_world';
  expect((await e.validate(source, example, new AbortController().signal)).report.valid).toBe(
    false,
  );
  failure = '';
  repeatable = false;
  expect((await e.validate(source, example, new AbortController().signal)).report).toMatchObject({
    valid: false,
    error: 'Generation is not repeatable',
  });
});
it('separates malformed imports and budget rejection from retryable isolation failure', async () => {
  const e = await executor();
  failure = 'import';
  expect((await e.validate(source, example, new AbortController().signal)).report).toMatchObject({
    valid: false,
    error: expect.stringContaining('Invalid import'),
  });
  failure = 'timeout';
  expect((await e.validate(source, example, new AbortController().signal)).report).toMatchObject({
    valid: false,
    error: expect.stringContaining('time limit'),
  });
  failure = 'isolation';
  await expect(e.validate(source, example, new AbortController().signal)).rejects.toThrow(
    'isolation failed',
  );
});
it('checks file hashes before inspecting room packages', async () => {
  const e = await executor();
  const calls = vi.mocked(runProcess).mock.calls.length;
  await expect(
    e.generate(
      source,
      {
        ...example,
        libraryId: '11111111-1111-4111-8111-111111111111',
        versionId: '22222222-2222-4222-8222-222222222222',
        generatorId: 'author:test',
        revision: 1,
        fileHash: 'ab'.repeat(32),
        packageHash: 'ab'.repeat(32),
      },
      new AbortController().signal,
    ),
  ).rejects.toThrow('file hash mismatch');
  expect(vi.mocked(runProcess).mock.calls).toHaveLength(calls);
});
