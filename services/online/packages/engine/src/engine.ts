// The glob2 headless binary as a set of async calls. Each call runs the
// binary once in a fresh scratch directory under the configured limits and
// returns parsed output; argument building and output parsing live in
// engineCli.ts.
import { readFile, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { gunzipSync } from 'node:zlib';
import { checkBuildingPackage, type BuildingPackage } from '@glob2/protocol';
import { canonicalBuildingJson, buildingAssetHash } from '@glob2/protocol/node';
import type { GeneratorDescriptor, ImportAiMapPayload, SimVersion } from '@glob2/protocol';
import { parse, ValidateSetResult } from '@glob2/protocol';
import {
  CATALOG_ARGS,
  requireCliVersion,
  parseBuildingComposition,
  type BuildingCompositionResult,
  EngineInputError,
  EngineOutputError,
  GENERATED_MAP_FILE,
  SIM_VERSION_ARGS,
  VERIFY_FILES,
  generateMapArgs,
  parseCatalog,
  parseGameResult,
  parseGenerationResult,
  parseMapReport,
  parseSimVersionOutput,
  parseVerdict,
  previewMapArgs,
  verifyMatchArgs,
  type EngineCatalog,
  type GameResult,
  type GenerationOutcome,
  type ReportMap,
  type VerifierVerdict,
} from './engineCli.ts';
import {
  outputTail,
  runProcess,
  withScratchDir,
  type ProcessLimits,
  type RunResult,
  type RunOptions,
} from './process.ts';

export type EngineCommand = 'catalog' | 'generate' | 'inspect' | 'verify';

export interface EngineOptions {
  /** Path of the glob2 binary. */
  binary: string;
  /** Working directory for the binary (holds its data/ directory). */
  workdir: string;
  /** Parent of per-job scratch directories (default: the OS temp dir). */
  scratchRoot?: string;
  /** Limits per command. */
  limits: Record<EngineCommand, ProcessLimits>;
  /** Optional isolation boundary; installed before leasing untrusted jobs. */
  processLauncher?: (options: RunOptions, scratch: string) => Promise<RunResult>;
  /** Largest file the agent reads back from a command's output directory. */
  maxOutputBytes: number;
}

/** Default limits: generous for 512×512 maps; verification scales with game length. */
export const DEFAULT_LIMITS: Record<EngineCommand, ProcessLimits> = {
  catalog: { timeoutMs: 60_000, cpuSeconds: 60, memoryMb: 2048, fileSizeMb: 64 },
  generate: { timeoutMs: 300_000, cpuSeconds: 600, memoryMb: 4096, fileSizeMb: 256 },
  inspect: { timeoutMs: 120_000, cpuSeconds: 120, memoryMb: 4096, fileSizeMb: 256 },
  verify: { timeoutMs: 3_600_000, cpuSeconds: 7200, memoryMb: 8192, fileSizeMb: 2048 },
};

/** The engine did not finish (timeout, crash or signal): may succeed on retry. */
export class EngineCrashError extends Error {
  override name = 'EngineCrashError';
  readonly result: RunResult;
  constructor(message: string, result: RunResult) {
    super(message);
    this.result = result;
  }
}

export interface GeneratedMap extends GenerationOutcome {
  /** Decompressed map bytes. */
  bytes: Uint8Array;
}

export interface Inspection {
  report: ReportMap;
  png?: Uint8Array;
}

export interface Verification {
  verdict: VerifierVerdict;
  /** Present for verified and diverged verdicts. */
  result?: { game: GameResult; json: Uint8Array };
  replay?: Uint8Array;
}

export class GlobEngine {
  readonly options: EngineOptions;
  constructor(options: EngineOptions) {
    this.options = options;
  }

  private async run(
    command: EngineCommand,
    args: string[],
    scratch: string,
    signal?: AbortSignal,
  ): Promise<RunResult> {
    const launch = this.options.processLauncher ?? ((options: RunOptions) => runProcess(options));
    const result = await launch(
      {
        binary: this.options.binary,
        args,
        cwd: this.options.workdir,
        env: {
          // Keep profiles, settings and logs inside the job's scratch directory.
          HOME: scratch,
          GLOB2_USER_DIR: join(scratch, 'profile'),
          SDL_VIDEODRIVER: 'dummy',
          SDL_AUDIODRIVER: 'dummy',
        },
        limits: this.options.limits[command],
        // The catalog is read from stdout (a few hundred KiB); other commands
        // write files and their output is only kept for diagnostics.
        maxCaptureBytes: command === 'catalog' ? 32 * 1024 * 1024 : 64 * 1024,
        ...(signal ? { signal } : {}),
      },
      scratch,
    );
    if (result.timedOut) {
      throw new EngineCrashError(
        `${args[0]} timed out after ${this.options.limits[command].timeoutMs} ms`,
        result,
      );
    }
    return result;
  }

  private scratch<T>(fn: (dir: string) => Promise<T>): Promise<T> {
    return withScratchDir(this.options.scratchRoot, fn);
  }

  /** Reads an output file, bounded; undefined when the command did not write it. */
  private async output(path: string): Promise<Uint8Array | undefined> {
    let size: number;
    try {
      size = (await stat(path)).size;
    } catch {
      return undefined;
    }
    if (size > this.options.maxOutputBytes) {
      throw new EngineOutputError(`${path} is ${size} bytes; limit ${this.options.maxOutputBytes}`);
    }
    return readFile(path);
  }

  async validateSet(bytes: Uint8Array, signal?: AbortSignal, gallery = false) {
    return this.scratch(async (dir) => {
      const input = join(dir, 'set.json'),
        reportPath = join(dir, 'report.json'),
        preview = join(dir, 'preview.png');
      await writeFile(input, bytes);
      const result = await this.run(
        'inspect',
        [
          'map',
          'validate-set',
          input,
          '--report-file',
          reportPath,
          '--preview',
          preview,
          ...(gallery ? ['--gallery', '1'] : []),
        ],
        dir,
        signal,
      );
      // Rejected packages still produce their structured validation report.
      if (result.code !== 0 && result.code !== 2) this.fail('set validation', result);
      const reportBytes = await this.output(reportPath);
      if (!reportBytes) throw new EngineOutputError('set validator omitted its report');
      const report = parse(
        ValidateSetResult,
        JSON.parse(Buffer.from(reportBytes).toString()),
        'set validation',
      );
      const png = report.valid ? await this.output(preview) : undefined;
      if (report.valid && !png) throw new EngineOutputError('set validator omitted its preview');
      return { report, png };
    });
  }

  private fail(what: string, result: RunResult): never {
    const detail = outputTail(result);
    if (result.code === null) {
      throw new EngineCrashError(
        `${what} killed by ${result.signal ?? 'signal'}: ${detail}`,
        result,
      );
    }
    if (result.code === 2 || result.code === 1) {
      // CLI 2 distinguishes rejected input from operational failures.
      throw new EngineInputError(`${what} refused the input (exit ${result.code}): ${detail}`);
    }
    throw new EngineCrashError(`${what} failed (exit ${result.code}): ${detail}`, result);
  }

  async catalog(signal?: AbortSignal): Promise<EngineCatalog> {
    return this.scratch(async (dir) => {
      const description = await this.run('catalog', ['help', '--format', 'json'], dir, signal);
      if (description.code !== 0) this.fail('CLI capability probe (requires CLI 2)', description);
      requireCliVersion(description.stdout);
      const result = await this.run('catalog', [...CATALOG_ARGS], dir, signal);
      if (result.code !== 0) this.fail('headless catalog', result);
      return parseCatalog(result.stdout);
    });
  }

  /** Capability-gated composition; retains the engine's exact canonical bytes. */
  async composeBuildings(
    packages: readonly BuildingPackage[],
    signal?: AbortSignal,
    artwork?: Uint8Array,
  ): Promise<BuildingCompositionResult> {
    const checked = packages.map(checkBuildingPackage);
    if (
      checked.length > 4096 ||
      checked.reduce((bytes, pkg) => bytes + Buffer.byteLength(canonicalBuildingJson(pkg)), 0) >
        8 * 1024 * 1024
    )
      throw new EngineInputError('Combined building manifests exceed their limits');
    const catalog = await this.catalog(signal);
    const baseHash = catalog.buildingCatalogHash;
    if (!catalog.commands.includes('compose_buildings') || !baseHash)
      throw new EngineInputError('This engine does not support building packages');
    return this.scratch(async (dir) => {
      const args = ['assets', 'compose-buildings', '--format', 'json'];
      for (let index = 0; index < checked.length; index++) {
        const path = join(dir, `package-${index}.json`);
        await writeFile(path, canonicalBuildingJson(checked[index]));
        args.push('--package', path);
      }
      let artworkHash: string | undefined;
      if (artwork !== undefined) {
        if (artwork.byteLength > 72 * 1024 * 1024)
          throw new EngineInputError('Artwork bundle exceeds 72 MiB');
        artworkHash = buildingAssetHash(artwork);
        const path = join(dir, 'artwork.g2ba');
        await writeFile(path, artwork);
        args.push('--artwork-bundle', path);
      }
      const result = await this.run('catalog', args, dir, signal);
      if (result.code !== 0) this.fail('building composition', result);
      return parseBuildingComposition(result.stdout, baseHash, artworkHash);
    });
  }

  /** Read the binary's simulation identity without changing the domain schema. */
  async reportedSimVersion(signal?: AbortSignal): Promise<SimVersion | undefined> {
    return this.scratch(async (dir) => {
      try {
        const result = await this.run('catalog', [...SIM_VERSION_ARGS], dir, signal);
        return result.code === 0 ? parseSimVersionOutput(result.stdout) : undefined;
      } catch (error) {
        if (error instanceof EngineCrashError) return undefined;
        throw error;
      }
    });
  }

  async generateMap(
    descriptor: GeneratorDescriptor,
    catalog: EngineCatalog,
    signal?: AbortSignal,
  ): Promise<GeneratedMap> {
    return this.scratch(async (dir) => {
      const out = join(dir, 'out');
      const result = await this.run(
        'generate',
        generateMapArgs(descriptor, catalog, out),
        dir,
        signal,
      );
      const resultJson = await this.output(join(out, 'result.json'));
      // The generator writes result.json with its diagnostic on refusals too.
      if (resultJson) {
        const outcome = parseGenerationResult(Buffer.from(resultJson).toString('utf8'));
        if (result.code !== 0) this.fail('map generation', result);
        const gz = await this.output(join(out, GENERATED_MAP_FILE));
        if (!gz) throw new EngineOutputError(`generator did not write ${GENERATED_MAP_FILE}`);
        return {
          ...outcome,
          bytes: gunzipSync(gz, { maxOutputLength: this.options.maxOutputBytes }),
        };
      }
      this.fail('map generation', result);
    });
  }

  /**
   * Loads a decompressed map or save with the game's loader (no simulation)
   * and returns its report, plus a PNG preview when previewSize is given.
   */
  async inspect(
    bytes: Uint8Array,
    options: { previewSize?: number; signal?: AbortSignal } = {},
  ): Promise<Inspection> {
    return this.scratch(async (dir) => {
      const input = join(dir, 'input.map');
      await writeFile(input, bytes);
      const reportPath = join(dir, 'report.json');
      const pngPath = options.previewSize ? join(dir, 'preview.png') : undefined;
      const result = await this.run(
        'inspect',
        previewMapArgs(input, {
          reportPath,
          ...(pngPath ? { pngPath, previewSize: options.previewSize } : {}),
        }),
        dir,
        options.signal,
      );
      if (result.code !== 0) this.fail('map loader', result);
      const report = await this.output(reportPath);
      if (!report) throw new EngineOutputError('map loader wrote no report');
      const inspection: Inspection = {
        report: parseMapReport(Buffer.from(report).toString('utf8')),
      };
      if (pngPath) {
        const png = await this.output(pngPath);
        if (!png) throw new EngineOutputError('map loader wrote no preview');
        inspection.png = png;
      }
      return inspection;
    });
  }

  async importImage(image: Uint8Array, payload: ImportAiMapPayload, signal?: AbortSignal) {
    return this.scratch(async (dir) => {
      const input = join(dir, 'candidate.png'),
        map = join(dir, 'map.map'),
        preview = join(dir, 'preview.png'),
        report = join(dir, 'report.json'),
        categorical = join(dir, 'categorical.png');
      await writeFile(input, image);
      const result = await this.run(
        'generate',
        [
          'map',
          'import-image',
          input,
          '--width',
          String(payload.settings.width),
          '--height',
          String(payload.settings.height),
          '--teams',
          String(payload.settings.players),
          '--seed',
          String(payload.seed),
          '--output',
          map,
          '--preview',
          preview,
          '--preview-size',
          '512',
          '--report-file',
          report,
        ],
        dir,
        signal,
      );
      if (result.code !== 0) this.fail('image import', result);
      const gz = await this.output(map + '.gz'),
        png = await this.output(preview),
        json = await this.output(report);
      if (!gz || !png || !json) throw new EngineOutputError('Image import omitted an output');
      const bytes = gunzipSync(gz, { maxOutputLength: this.options.maxOutputBytes });
      const exported = await this.run(
        'inspect',
        ['map', 'export-image', map + '.gz', '--output', categorical],
        dir,
        signal,
      );
      if (exported.code !== 0) this.fail('categorical export', exported);
      const tile = await this.output(categorical);
      if (!tile) throw new EngineOutputError('Image import omitted categorical output');
      const inspection = await this.inspect(bytes, { signal });
      return { bytes, png, json, tile, map: inspection.report };
    });
  }

  async verifyMatch(
    record: Uint8Array,
    map: Uint8Array,
    signal?: AbortSignal,
  ): Promise<Verification> {
    return this.scratch(async (dir) => {
      const recordPath = join(dir, 'match.g2mr');
      const mapPath = join(dir, 'map.map');
      const out = join(dir, 'out');
      await writeFile(recordPath, record);
      await writeFile(mapPath, map);
      const result = await this.run(
        'verify',
        verifyMatchArgs(recordPath, mapPath, out),
        dir,
        signal,
      );
      const verdictJson = await this.output(join(out, VERIFY_FILES.verdict));
      if (!verdictJson)
        this.fail('match verifier', result.code === 0 ? { ...result, code: 3 } : result);
      const verdict = parseVerdict(Buffer.from(verdictJson).toString('utf8'));
      const verification: Verification = { verdict };
      if (verdict.verdict !== 'unverifiable') {
        const json = await this.output(join(out, VERIFY_FILES.result));
        const replay = await this.output(join(out, VERIFY_FILES.replay));
        if (!json || !replay) {
          throw new EngineOutputError(
            `verifier reported ${verdict.verdict} without ${!json ? VERIFY_FILES.result : VERIFY_FILES.replay}`,
          );
        }
        verification.result = { game: parseGameResult(Buffer.from(json).toString('utf8')), json };
        verification.replay = replay;
      }
      return verification;
    });
  }
}
