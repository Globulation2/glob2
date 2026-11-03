// Uploaded sheets are decoded in a disposable Linux namespace, with no network
// and only the engine, installed data and this job's scratch directory mounted.
import { access, readFile, stat, statfs, writeFile } from 'node:fs/promises';
import { join, resolve, dirname, delimiter } from 'node:path';
import { tmpdir } from 'node:os';
import {
  parse,
  ValidateSetResult,
  SET_PACKAGE_MAX_BYTES,
  type ValidateSetResult as Report,
} from '@glob2/protocol';
import { runProcess, withScratchDir, type RunOptions, type RunResult } from '@glob2/engine/process';
import type { EngineOptions } from '@glob2/engine/engine';
export type SetValidator = (
  bytes: Uint8Array,
  signal: AbortSignal,
) => Promise<{ report: Report; png?: Uint8Array }>;
/** Shared by map inspection, preview, match verification and set publishing: maps
 * can embed the same untrusted PNGs as standalone sets. Never decode them first. */
export async function createAssetSandbox(
  options: EngineOptions,
): Promise<(command: RunOptions, scratch: string) => Promise<RunResult>> {
  if (process.platform !== 'linux')
    throw Error('Custom asset processing requires Linux namespace isolation');
  const filesystem = await statfs(options.scratchRoot ?? tmpdir());
  if (filesystem.type !== 0x01021994 || filesystem.blocks * filesystem.bsize > 4 * 1024 ** 3)
    throw Error('Set validation requires a dedicated scratch tmpfs of at most 4 GiB');
  const binary = '/usr/bin/bwrap';
  await access(binary);
  const mounts: string[] = [];
  for (const directory of ['/usr', '/lib', '/lib64', '/opt/glob2/lib'])
    try {
      await access(directory);
      mounts.push('--ro-bind', directory, directory);
    } catch {
      /* optional ABI path */
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
    '--dir',
    '/game',
    '--ro-bind',
    resolve(options.workdir, 'data'),
    '/game/data',
    '--ro-bind',
    resolve(options.binary),
    '/engine',
    '--chdir',
    '/game',
    '--setenv',
    'HOME',
    '/job',
    '--setenv',
    'GLOB2_USER_DIR',
    '/job/profile',
    '--setenv',
    'SDL_VIDEODRIVER',
    'dummy',
    '--setenv',
    'SDL_AUDIODRIVER',
    'dummy',
  ];
  const configuredLibraries =
    process.env['ENGINE_SET_LIBRARY_PATH'] ??
    process.env['ENGINE_AI_LIBRARY_PATH'] ??
    process.env['LD_LIBRARY_PATH'];
  const libraries = configuredLibraries
    ? configuredLibraries
        .split(delimiter)
        .filter(Boolean)
        .map((p) => resolve(p))
    : [];
  const bundledLibraries = join(dirname(dirname(resolve(options.binary))), 'lib', 'glob2');
  try {
    await access(bundledLibraries);
    libraries.push(bundledLibraries);
  } catch {
    /* system-linked binary */
  }
  if (libraries.length > 8) throw Error('Too many validator runtime library directories');
  for (const libraryPath of new Set(libraries)) {
    if (!(await stat(libraryPath)).isDirectory())
      throw Error('Validator library path must be a directory');
    base.push('--ro-bind', resolve(libraryPath), resolve(libraryPath));
  }
  if (libraries.length)
    base.push('--setenv', 'LD_LIBRARY_PATH', [...new Set(libraries)].join(delimiter));
  return (command, scratch) =>
    runProcess({
      ...command,
      binary,
      args: [
        ...base,
        '--bind',
        scratch,
        '/job',
        '--',
        '/engine',
        ...command.args.map((arg) =>
          arg === scratch
            ? '/job'
            : arg.startsWith(scratch + '/')
              ? '/job' + arg.slice(scratch.length)
              : arg,
        ),
      ],
      cwd: options.workdir,
      // The namespace's clearenv drops the wrapper environment and agent secrets.
      env: {},
    });
}

export async function createSetValidator(
  options: EngineOptions,
  launcher = options.processLauncher,
): Promise<SetValidator> {
  const launch = launcher ?? (await createAssetSandbox(options));
  async function run(scratch: string, args: string[], signal?: AbortSignal) {
    const result = await launch(
      {
        binary: options.binary,
        args,
        cwd: options.workdir,
        limits: { timeoutMs: 120000, cpuSeconds: 120, memoryMb: 2048, fileSizeMb: 64 },
        ...(signal ? { signal } : {}),
        maxCaptureBytes: 65536,
      },
      scratch,
    );
    if (
      (result.code !== 0 &&
        !(args[0] === 'map' && args[1] === 'validate-set' && result.code === 2)) ||
      result.timedOut
    )
      throw Error('Isolated asset validator failed: ' + result.stderr.slice(-1000));
  }
  // Exercise rendering as well as startup before advertising the job.
  await withScratchDir(options.scratchRoot, async (dir) => {
    await run(dir, ['info', 'sim-version', '--format', 'json']);
    const setId = '11111111-1111-4111-8111-111111111111',
      versionId = '22222222-2222-4222-8222-222222222222';
    await writeFile(
      join(dir, 'probe.json'),
      JSON.stringify({
        schemaVersion: 1,
        setId,
        versionId,
        title: 'Validator probe',
        description: '',
        tags: [],
        license: 'CC0-1.0',
        credits: [{ author: 'Glob2', license: 'CC0-1.0' }],
        resources: [],
        terrains: [
          {
            key: 's' + setId.replaceAll('-', '') + versionId.replaceAll('-', '') + ':grass',
            name: 'Probe grass',
            base: 'grass',
            appearance: 'grass',
            properties: {},
          },
        ],
        assets: { schemaVersion: 1, sheets: [], terrains: {}, credits: [] },
      }),
    );
    await run(dir, [
      'map',
      'validate-set',
      '/job/probe.json',
      '--report-file',
      '/job/probe-report.json',
      '--preview',
      '/job/probe.png',
    ]);
    const report = parse(
      ValidateSetResult,
      JSON.parse(await readFile(join(dir, 'probe-report.json'), 'utf8')),
      'probe report',
    );
    if (!report.valid || (await stat(join(dir, 'probe.png'))).size > 4 * 1024 * 1024)
      throw Error('Isolated set rendering probe failed');
  });
  return (bytes, signal) =>
    withScratchDir(options.scratchRoot, async (dir) => {
      if (bytes.byteLength > SET_PACKAGE_MAX_BYTES) throw Error('Set exceeds package limit');
      await writeFile(join(dir, 'set.json'), bytes);
      await run(
        dir,
        [
          'map',
          'validate-set',
          '/job/set.json',
          '--report-file',
          '/job/report.json',
          '--preview',
          '/job/preview.png',
        ],
        signal,
      );
      async function output(name: string, maximum: number) {
        const path = join(dir, name),
          info = await stat(path);
        if (!info.isFile() || info.size > maximum)
          throw Error('Set validation output exceeds limit');
        return readFile(path);
      }
      const report = parse(
        ValidateSetResult,
        JSON.parse((await output('report.json', 8192)).toString()),
        'set report',
      );
      return {
        report,
        ...(report.valid ? { png: await output('preview.png', 4 * 1024 * 1024) } : {}),
      };
    });
}
