import { afterEach, expect, it, vi } from 'vitest';
import type * as FsPromises from 'node:fs/promises';
import type * as EngineProcess from '@glob2/engine/process';
import { runProcess } from '@glob2/engine/process';
import { DEFAULT_LIMITS } from '../src/engine.ts';
import { createAssetSandbox } from '../src/setValidation.ts';

vi.mock('node:fs/promises', async (original) => ({
  ...(await original<typeof FsPromises>()),
  access: async () => {},
  stat: async () => ({ isDirectory: () => true }),
  statfs: async () => ({ type: 0x01021994, blocks: 1024, bsize: 4096 }),
}));
vi.mock('@glob2/engine/process', async (original) => ({
  ...(await original<typeof EngineProcess>()),
  runProcess: vi.fn(async () => ({
    code: 0,
    signal: null,
    timedOut: false,
    stdout: '',
    stderr: '',
    ms: 1,
  })),
}));
const platform = Object.getOwnPropertyDescriptor(process, 'platform')!;
afterEach(() => Object.defineProperty(process, 'platform', platform));

it('isolates every map command and rewrites only private scratch paths', async () => {
  Object.defineProperty(process, 'platform', { value: 'linux' });
  const launch = await createAssetSandbox({
    binary: '/opt/glob2/bin/glob2',
    workdir: '/opt/glob2',
    limits: DEFAULT_LIMITS,
    maxOutputBytes: 65536,
  });
  const signal = new AbortController().signal;
  for (const flag of ['--preview-map', '--verify-match', '--import-map-image']) {
    await launch(
      {
        binary: '/opt/glob2/bin/glob2',
        cwd: '/opt/glob2',
        args: [
          flag,
          '/private/job/input.map',
          '--output',
          '/private/job/out',
          '/private/job-other',
        ],
        env: { DATABASE_URL: 'must-not-pass' },
        limits: DEFAULT_LIMITS.inspect,
        signal,
      },
      '/private/job',
    );
    const command = vi.mocked(runProcess).mock.calls.at(-1)![0];
    expect(command.binary).toBe('/usr/bin/bwrap');
    expect(command.args).toEqual(
      expect.arrayContaining([
        '--unshare-all',
        '--clearenv',
        '--cap-drop',
        'ALL',
        '--ro-bind',
        '/opt/glob2/data',
        '/game/data',
        '--bind',
        '/private/job',
        '/job',
        flag,
        '/job/input.map',
        '/job/out',
        '/private/job-other',
      ]),
    );
    expect(command.env).toEqual({});
    expect(command.signal).toBe(signal);
    expect(command.limits).toEqual(DEFAULT_LIMITS.inspect);
  }
});

it('refuses unavailable host isolation before running any decoder', async () => {
  Object.defineProperty(process, 'platform', { value: 'darwin' });
  const before = vi.mocked(runProcess).mock.calls.length;
  await expect(
    createAssetSandbox({
      binary: '/engine',
      workdir: '/',
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 65536,
    }),
  ).rejects.toThrow(/Linux namespace/);
  expect(vi.mocked(runProcess).mock.calls).toHaveLength(before);
});
