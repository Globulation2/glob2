// Every package operation, including module inspection, stays inside the namespace.
import { createHash } from 'node:crypto';
import { readFile, writeFile, stat, mkdir, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { gunzipSync } from 'node:zlib';
import { withScratchDir } from '@glob2/engine/process';
import type { EngineOptions } from '@glob2/engine/engine';
import { parseGenerationResult } from '@glob2/engine/engineCli';
import {
  GENERATOR_MAX_BYTES,
  GeneratorMetadata,
  parse,
  pendingGeneratorReport,
  simVersionKey,
  type GeneratorSettings,
  type GeneratorMetadata as Metadata,
  type GeneratorValidationReport,
  type ScriptGeneratorDescriptor,
  type SimVersion,
} from '@glob2/protocol';
import { createAssetSandbox } from './setValidation.ts';
import { EngineInputError } from './engineCli.ts';
const hash = (bytes: Uint8Array) => createHash('sha256').update(bytes).digest('hex');
const diagnostic = (e: unknown) => (e instanceof Error ? e.message : String(e)).slice(0, 1900);
export interface GeneratorExecutor {
  validate(
    bytes: Uint8Array,
    example: GeneratorSettings,
    signal: AbortSignal,
  ): Promise<{ report: GeneratorValidationReport; canonical?: Uint8Array; png?: Uint8Array }>;
  generate(
    bytes: Uint8Array,
    descriptor: ScriptGeneratorDescriptor,
    signal: AbortSignal,
  ): Promise<{
    bytes: Uint8Array;
    chosenSeed: number;
    map: ReturnType<typeof parseGenerationResult>['map'];
    packageHash: string;
    startQuality?: { fairness: number; score: number };
  }>;
}
export function generatorMatrix(
  example: GeneratorSettings,
  metadata: Metadata,
): GeneratorSettings[] {
  const change = (params: Record<string, number>, seed = example.seed): GeneratorSettings => ({
    ...example,
    seed,
    candidates: 1,
    params: { ...example.params, ...params },
  });
  const rows = [change({}, (example.seed + 1) >>> 0), change({}, (example.seed + 2) >>> 0)];
  const width = example.params.width ?? 7,
    height = example.params.height ?? 7;
  rows.push(
    change({ width: Math.max(6, width - 1), height: Math.max(6, height - 1) }),
    change({ width: Math.min(9, width + 1), height: Math.min(9, height + 1) }),
    change({ width: 7, height: 8 }),
  );
  if (!metadata.editorOnly) rows.push(change({ teams: 2 }), change({ teams: 8 }));
  for (const control of metadata.controls) {
    const max =
      control.kind === 'choice'
        ? (control.choices?.length ?? 1) - 1
        : control.kind === 'toggle'
          ? 1
          : control.values?.length
            ? Math.max(...control.values)
            : (control.maximum ?? control.default);
    const min =
      control.kind === 'range'
        ? control.values?.length
          ? Math.min(...control.values)
          : (control.minimum ?? control.default)
        : 0;
    rows.push(change({ [control.id]: min }), change({ [control.id]: max }));
  }
  return rows;
}
export async function createGeneratorExecutor(
  options: EngineOptions,
  sim: SimVersion,
): Promise<GeneratorExecutor> {
  const launch = await createAssetSandbox(options);
  async function run(dir: string, args: string[], signal?: AbortSignal) {
    const result = await launch(
      {
        binary: options.binary,
        args,
        cwd: options.workdir,
        limits: { timeoutMs: 120000, cpuSeconds: 120, memoryMb: 2048, fileSizeMb: 64 },
        maxCaptureBytes: 65536,
        ...(signal ? { signal } : {}),
      },
      dir,
    );
    if (signal?.aborted) throw Error('Generator job cancelled');
    if (result.code !== 0 && result.stderr.startsWith('bwrap:'))
      throw Error('Generator isolation failed: ' + result.stderr.slice(-1000));
    if (result.timedOut) throw new EngineInputError('Generator exceeded execution time limit');
    return result;
  }
  async function output(dir: string, name: string, limit = 64 * 1024 * 1024) {
    const path = join(dir, name),
      info = await stat(path);
    if (!info.isFile() || info.size > limit)
      throw new EngineInputError('Generator output exceeds limit');
    return readFile(path);
  }
  async function inspect(dir: string, bytes: Uint8Array, signal?: AbortSignal) {
    if (bytes.byteLength > GENERATOR_MAX_BYTES)
      throw new EngineInputError('Generator exceeds 4 MiB');
    await writeFile(join(dir, 'input.json'), bytes);
    const result = await run(
      dir,
      [
        'map',
        'inspect-package',
        join(dir, 'input.json'),
        '--output',
        join(dir, 'canonical.json'),
        '--report-file',
        join(dir, 'metadata.json'),
      ],
      signal,
    );
    if (result.code !== 0)
      throw new EngineInputError('Invalid generator package: ' + result.stderr.slice(-1000));
    const canonical = await output(dir, 'canonical.json', GENERATOR_MAX_BYTES);
    const raw = JSON.parse((await output(dir, 'metadata.json', 8 * 1024 * 1024)).toString());
    const metadata = parse(
      GeneratorMetadata,
      {
        ...raw,
        controls: raw.controls.map((c: Record<string, unknown>) => ({
          ...c,
          group: c.group ?? 'terrain',
        })),
      },
      'generator metadata',
    );
    if (raw.packageHash !== hash(canonical)) throw Error('Engine canonical package hash mismatch');
    return { canonical, metadata, packageHash: raw.packageHash as string };
  }
  async function generate(
    dir: string,
    name: string,
    metadata: Metadata,
    settings: GeneratorSettings,
    signal?: AbortSignal,
  ) {
    const out = join(dir, name);
    await mkdir(out);
    const args = [
      'map',
      'study',
      metadata.id,
      '--generator-package',
      join(dir, 'canonical.json'),
      '--seed',
      String(settings.seed),
      '--candidates',
      String(settings.candidates),
      '--write-map',
    ];
    for (const [key, value] of Object.entries(settings.params))
      args.push('--set', `${key}=${value}`);
    args.push('--output-dir', out);
    const result = await run(dir, args, signal);
    let document;
    try {
      document = JSON.parse(
        (await output(dir, name + '/result.json', 16 * 1024 * 1024)).toString(),
      );
    } catch (error) {
      if (result.code !== 0)
        throw new EngineInputError('Generator failed: ' + result.stderr.slice(-1000));
      throw error;
    }
    if (document.status !== 'completed') {
      const reason = document.map_report?.generation?.outcome?.error ?? document.status;
      const refused = [
        'refused',
        'request_refused',
        'world_refused',
        'invalid_request',
        'placement_failed',
        'no_candidate',
        'no_valid_candidate',
      ].includes(reason);
      return {
        status: refused ? ('refused' as const) : ('failed' as const),
        message: String(
          document.map_report?.generation?.outcome?.message ?? document.diagnostic ?? reason,
        ).slice(0, 1900),
      };
    }
    if (result.code !== 0) throw new EngineInputError('Generator process crashed');
    const bytes = gunzipSync(await output(dir, name + '/map-r0.map.gz'), {
      maxOutputLength: 64 * 1024 * 1024,
    });
    return { status: 'passed' as const, bytes, document };
  }
  async function reload(dir: string, name: string, signal?: AbortSignal) {
    const result = await run(
      dir,
      [
        'map',
        'preview',
        join(dir, name, 'map-r0.map.gz'),
        '--report-file',
        join(dir, name, 'loaded.json'),
        '--output',
        join(dir, name, 'preview.png'),
        '--preview-size',
        '512',
      ],
      signal,
    );
    if (result.code !== 0)
      throw new EngineInputError(
        'Generated map could not be loaded: ' + result.stderr.slice(-1000),
      );
    return output(dir, name + '/preview.png', 4 * 1024 * 1024);
  }
  const executor: GeneratorExecutor = {
    validate: (bytes, example, signal) =>
      withScratchDir(options.scratchRoot, async (dir) => {
        const report = pendingGeneratorReport(hash(bytes), simVersionKey(sim));
        const callerSignal = signal;
        const deadline = AbortSignal.timeout(15 * 60 * 1000);
        signal = AbortSignal.any([signal, deadline]);
        try {
          const inspected = await inspect(dir, bytes, signal);
          Object.assign(report, {
            metadata: inspected.metadata,
            packageHash: inspected.packageHash,
            fileHash: hash(inspected.canonical),
          });
          const matrix = [example, ...generatorMatrix(example, inspected.metadata)];
          let png: Uint8Array | undefined;
          for (const [index, settings] of matrix.entries()) {
            const a = await generate(dir, `sample-${index}`, inspected.metadata, settings, signal);
            if (a.status !== 'passed') {
              report.samples.push({ settings, status: a.status, message: a.message });
              if (index === 0 || a.status === 'failed') throw new EngineInputError(a.message);
              await rm(join(dir, `sample-${index}`), { recursive: true });
              continue;
            }
            const b = await generate(dir, `repeat-${index}`, inspected.metadata, settings, signal);
            if (b.status !== 'passed' || !a.bytes.equals(b.bytes)) {
              report.samples.push({
                settings,
                status: 'failed',
                message: 'Repeated generation produced different maps',
              });
              throw new EngineInputError('Generation is not repeatable');
            }
            const preview = await reload(dir, `sample-${index}`, signal);
            if (index === 0) png = preview;
            report.samples.push({ settings, status: 'passed', fingerprint: hash(a.bytes) });
            await rm(join(dir, `sample-${index}`), { recursive: true });
            await rm(join(dir, `repeat-${index}`), { recursive: true });
          }
          report.valid = true;
          return { report, canonical: inspected.canonical, ...(png ? { png } : {}) };
        } catch (error) {
          const problem =
            deadline.aborted && !callerSignal.aborted
              ? new EngineInputError('Generator exceeded total validation time limit')
              : error;
          if (!(problem instanceof EngineInputError)) throw problem;
          report.error = diagnostic(problem);
          return { report };
        }
      }),
    generate: (bytes, descriptor, signal) =>
      withScratchDir(options.scratchRoot, async (dir) => {
        if (hash(bytes) !== descriptor.fileHash)
          throw new EngineInputError('Package file hash mismatch');
        const inspected = await inspect(dir, bytes, signal);
        if (
          inspected.packageHash !== descriptor.packageHash ||
          inspected.metadata.id !== descriptor.generatorId ||
          inspected.metadata.revision !== descriptor.revision ||
          inspected.metadata.editorOnly
        )
          throw new EngineInputError('Generator release identity mismatch or editor-only package');
        const result = await generate(dir, 'map', inspected.metadata, descriptor, signal);
        if (result.status !== 'passed') throw new EngineInputError(result.message);
        await reload(dir, 'map', signal);
        return {
          ...parseGenerationResult(JSON.stringify(result.document)),
          bytes: result.bytes,
          packageHash: inspected.packageHash,
        };
      }),
  };
  // Exercise uploaded module initialization, canonical export, generation and reload.
  const probe = Buffer.from(
    JSON.stringify({
      formatVersion: 1,
      manifest: {
        id: 'glob2:validation-probe',
        name: 'Probe',
        apiVersion: 1,
        revision: 1,
        entry: 'generator.js',
        tags: ['terrain:natural', 'style:wide-open'],
        editorOnly: true,
        hasStartingColonies: false,
        controls: [],
      },
      modules: {
        'generator.js':
          'export function generate(c) { c.addTeams(); c.toolkit.Sketch.writeVertices(c.mask(c.torus.size(), 2)); }',
      },
    }),
  );
  await withScratchDir(options.scratchRoot, async (dir) => {
    const inspected = await inspect(dir, probe);
    const result = await generate(dir, 'probe', inspected.metadata, {
      seed: 19,
      params: { width: 6, height: 6, teams: 2, workers: 4 },
      candidates: 1,
      startingUnitLevel: 0,
    });
    if (result.status !== 'passed')
      throw Error('Isolated generator startup probe failed: ' + result.message);
    await reload(dir, 'probe');
  });
  return executor;
}
