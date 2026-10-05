import {
  access,
  readFile,
  writeFile,
  statfs,
  lstat,
  mkdir,
  realpath,
  readlink,
} from 'node:fs/promises';
import { resolve, join, dirname } from 'node:path';
import { runProcess, withScratchDir } from '@glob2/engine/process';
import type { MusicMetadata, MusicStudioSettings } from '@glob2/protocol';
export interface CandidateReport {
  metadata?: MusicMetadata;
  passed: boolean;
  checks: {
    name: string;
    status: string;
    measures: {
      name: string;
      status: string;
      value: unknown;
      threshold: string;
      detail: string;
      unit?: string;
    }[];
  }[];
  result?: {
    timelineId?: string;
    frames: number;
    tracks: {
      mood: 'calm' | 'building' | 'combat';
      sha256: string;
      bytes: number;
      url: string;
      waveform: number[];
    }[];
    warnings: string[];
  };
}
export interface Rendered {
  report: CandidateReport;
  files: Record<string, Buffer>;
  score: string;
}
export interface MusicRunner {
  run(
    source: string,
    settings: MusicStudioSettings,
    render: boolean,
    signal: AbortSignal,
    progress: (event: Record<string, unknown>) => void,
    metadata: MusicMetadata & { id: string; origin: string },
  ): Promise<Rendered>;
}
/** Each invocation is a fresh namespace. Generated code never runs with the
 * controller's database/blob/provider credentials or in the trusted QA process. */
export async function createRunner(
  root: string,
  assets: string,
  scratch: string,
  python: string,
): Promise<MusicRunner> {
  if (process.platform !== 'linux')
    throw Error('Music authoring requires Linux namespace isolation.');
  const fs = await statfs(scratch);
  if (fs.type !== 0x01021994 || fs.blocks * fs.bsize > 6 * 1024 ** 3)
    throw Error('Music authoring requires a dedicated scratch tmpfs of at most 6 GiB.');
  await access('/usr/bin/bwrap');
  await access('/usr/bin/prlimit');
  await access(assets);
  const mounts: string[] = [];
  for (const dir of new Set([
    '/usr',
    '/lib',
    '/lib64',
    '/opt/music',
    dirname(dirname(resolve(python))),
    dirname(dirname(await realpath(python))),
    dirname(dirname(resolve(dirname(python), await readlink(python).catch(() => python)))),
  ])) {
    try {
      await access(dir);
      mounts.push('--ro-bind', dir, dir);
    } catch {
      /* optional library path */
    }
  }
  const base = [
    ...mounts,
    '--unshare-all',
    '--unshare-user',
    '--disable-userns',
    '--cap-drop',
    'ALL',
    '--die-with-parent',
    '--new-session',
    '--clearenv',
    '--proc',
    '/proc',
    '--dev',
    '/dev',
    '--size',
    '67108864',
    '--tmpfs',
    '/tmp',
    '--ro-bind',
    resolve(root),
    '/pipeline/tools/music',
    '--ro-bind',
    resolve(root, '../encode_music.py'),
    '/pipeline/tools/encode_music.py',
    '--ro-bind',
    resolve(assets),
    '/assets',
    '--setenv',
    'PATH',
    '/opt/music/bin:/usr/bin',
    '--setenv',
    'PYTHONPATH',
    '/pipeline/tools/music',
    '--setenv',
    'PYTHONDONTWRITEBYTECODE',
    '1',
    '--setenv',
    'OPENBLAS_NUM_THREADS',
    '1',
    '--setenv',
    'OMP_NUM_THREADS',
    '1',
    '--setenv',
    'MKL_NUM_THREADS',
    '1',
    '--setenv',
    'NUMEXPR_NUM_THREADS',
    '1',
    '--setenv',
    'NUMBA_NUM_THREADS',
    '1',
    '--setenv',
    'NUMBA_CACHE_DIR',
    '/tmp/numba',
    '--setenv',
    'HOME',
    '/tmp',
    '--chdir',
    '/job',
  ];
  async function invoke(
    job: string,
    command: string,
    settings: MusicStudioSettings,
    signal: AbortSignal,
    progress?: (event: Record<string, unknown>) => void,
  ) {
    let pending = '';
    const result = await runProcess({
      binary: '/usr/bin/bwrap',
      args: [
        ...base,
        '--bind',
        job,
        '/job',
        '--remount-ro',
        '/',
        '--',
        '/usr/bin/prlimit',
        '--nproc=64',
        '--',
        python,
        '-m',
        'glob2music.studio',
        command,
        '--pipeline',
        settings.pipeline,
        '--seed',
        String(settings.seed),
      ],
      cwd: scratch,
      signal,
      limits: {
        timeoutMs: command === 'render' ? 20 * 60_000 : 60_000,
        cpuSeconds: 1200,
        memoryMb: 8192,
        fileSizeMb: 256,
      },
      maxCaptureBytes: 16000,
      ...(progress
        ? {
            onStdout: (chunk: Buffer) => {
              pending += chunk.toString();
              if (pending.length > 65536) {
                pending = '';
                return;
              }
              const lines = pending.split('\n');
              pending = lines.pop() ?? '';
              for (const line of lines) {
                try {
                  const value: unknown = JSON.parse(line);
                  if (value && typeof value === 'object')
                    progress(value as Record<string, unknown>);
                } catch {
                  /* not a progress record */
                }
              }
            },
          }
        : {}),
    });
    if (result.code !== 0)
      throw Error(
        result.timedOut
          ? 'Music execution time limit exceeded.'
          : result.stderr.slice(-3000) || 'Music subprocess failed.',
      );
  }
  await withScratchDir(scratch, async (job) => {
    await invoke(job, 'probe', { pipeline: 'acoustic-v1', seed: 0 }, AbortSignal.timeout(60000));
  });
  return {
    async run(source, settings, render, signal, progress, metadata) {
      if (Buffer.byteLength(source) > 128 * 1024)
        throw Error('Composition source exceeds 128 KiB.');
      return withScratchDir(scratch, async (job) => {
        const untrusted = join(job, 'untrusted'),
          trusted = join(job, 'trusted');
        await mkdir(untrusted);
        await mkdir(trusted);
        await writeFile(join(untrusted, 'composition.py'), source, { flag: 'wx' });
        await invoke(untrusted, 'export', settings, signal);
        // No symlinks, devices or arbitrary filenames cross into the fresh trusted job.
        const info = await lstat(join(untrusted, 'score.json'));
        if (!info.isFile() || info.isSymbolicLink() || info.size > 4 * 1024 * 1024)
          throw Error('Invalid exported score.');
        const score = await readFile(join(untrusted, 'score.json'), 'utf8');
        await writeFile(join(trusted, 'score.json'), score, { flag: 'wx' });
        await writeFile(join(trusted, 'metadata.json'), JSON.stringify(metadata), { flag: 'wx' });
        await invoke(trusted, render ? 'render' : 'check', settings, signal, progress);
        const report = JSON.parse(
          await readFile(join(trusted, 'result.json'), 'utf8'),
        ) as CandidateReport;
        const files: Record<string, Buffer> = {};
        if (render && report.result)
          for (const name of [
            'a1.opus',
            'a2.opus',
            'a3.opus',
            'preview.opus',
            'waveforms.json',
            'set.zip',
          ]) {
            const path = join(trusted, 'output', name),
              info = await lstat(path);
            if (!info.isFile() || info.size > 32 * 1024 * 1024)
              throw Error('Invalid rendered artifact.');
            files[name] = await readFile(path);
          }
        return { report, files, score };
      });
    },
  };
}
