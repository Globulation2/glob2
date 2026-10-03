// The real EngineRunner: each job kind as glob2 headless commands plus blob
// store I/O. Inputs arrive already checked against the job schema (the agent
// parses every EngineJob); every result is checked against its kind's result
// schema before it is reported, so a contract break is caught here rather
// than recorded as a failure by the worker.
import {
  engineJobs,
  sameSimVersion,
  schemaIssues,
  simVersionKey,
  type EngineJob,
  type EngineJobKind,
  type EngineJobOutput,
  type EngineJobPayload,
  type SimVersion,
  type VerifiedOutcome,
} from '@glob2/protocol';
import { checkMapFile } from '@glob2/core';
import { EngineJobError, type EngineRunner } from './agent.ts';
import { CONTENT_TYPES, decompressIfGzip, type JobBlobs } from './blobs.ts';
import { EngineCrashError, type GlobEngine } from './engine.ts';
import {
  EngineInputError,
  EngineOutputError,
  PREVIEW_SIZE_RANGE,
  savedPlayers,
  type EngineCatalog,
} from './engineCli.ts';

export interface RunnerLimits {
  /** Largest map or save, decompressed (and as stored), in bytes. */
  maxMapBytes: number;
  /** Largest match record in bytes. */
  maxRecordBytes: number;
  /** Largest accepted map side in tiles (the engine's generators stop at 512). */
  maxMapSide: number;
}

export const DEFAULT_RUNNER_LIMITS: RunnerLimits = {
  maxMapBytes: 64 * 1024 * 1024,
  maxRecordBytes: 256 * 1024 * 1024,
  maxMapSide: 512,
};

export interface HeadlessRunnerOptions {
  engine: GlobEngine;
  catalog: EngineCatalog;
  simVersion: SimVersion;
  /** Blob access for run() calls that pass none (tests); the agent passes each lease's own. */
  blobs?: JobBlobs;
  limits?: Partial<RunnerLimits>;
}

const MAX_MESSAGE = 1900;

function message(text: string): string {
  return text.length > MAX_MESSAGE ? `${text.slice(0, MAX_MESSAGE)}…` : text;
}

/** Reads width and height from a PNG's IHDR chunk. */
export function pngSize(bytes: Uint8Array): { width: number; height: number } {
  const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  if (bytes.length < 24 || signature.some((b, i) => bytes[i] !== b)) {
    throw new EngineOutputError('preview is not a PNG');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return { width: view.getUint32(16), height: view.getUint32(20) };
}

export class HeadlessEngineRunner implements EngineRunner {
  readonly kinds: readonly EngineJobKind[] = [
    'import-ai-map',
    'generate-map',
    'validate-map',
    'render-preview',
    'verify-match',
  ];
  private readonly limits: RunnerLimits;
  private readonly options: HeadlessRunnerOptions;

  constructor(options: HeadlessRunnerOptions) {
    this.options = options;
    this.limits = { ...DEFAULT_RUNNER_LIMITS, ...options.limits };
  }

  async run(job: EngineJob, signal: AbortSignal, jobBlobs?: JobBlobs): Promise<unknown> {
    const blobs = jobBlobs ?? this.options.blobs;
    if (!blobs) throw new Error('no blob access for this job');
    let result: unknown;
    try {
      switch (job.kind) {
        case 'import-ai-map':
          result = await this.importImage(job.payload, signal, blobs);
          break;
        case 'generate-map':
          result = await this.generateMap(job.payload, signal, blobs);
          break;
        case 'validate-map':
          result = await this.validateMap(job.payload, signal, blobs);
          break;
        case 'render-preview':
          result = await this.renderPreview(job.payload, signal, blobs);
          break;
        case 'verify-match':
          result = await this.verifyMatch(job.payload, signal, blobs);
          break;
      }
    } catch (error) {
      if (error instanceof EngineInputError)
        throw new EngineJobError('bad_request', message(error.message));
      if (error instanceof EngineOutputError)
        throw new EngineJobError('internal', message(error.message));
      throw error;
    }
    const issues = schemaIssues(engineJobs[job.kind].result, result);
    if (issues.length > 0) {
      throw new EngineJobError(
        'internal',
        message(
          `${job.kind} result breaks the contract: ${issues.map((i) => `${i.path} ${i.message}`).join('; ')}`,
        ),
      );
    }
    return result;
  }

  private checkFacts(facts: {
    width: number;
    height: number;
    teamCount: number;
  }): string | undefined {
    if (facts.width > this.limits.maxMapSide || facts.height > this.limits.maxMapSide) {
      return `This map is too large: it is ${facts.width}×${facts.height}, and the largest accepted side is ${this.limits.maxMapSide}.`;
    }
    if (facts.width < 1 || facts.height < 1) return 'This map has no tiles.';
    if (facts.teamCount < 1 || facts.teamCount > 12)
      return `This map has ${facts.teamCount} teams; maps need 1 to 12.`;
    return undefined;
  }

  async importImage(
    payload: EngineJobPayload<'import-ai-map'>,
    signal: AbortSignal,
    blobs: JobBlobs,
  ): Promise<EngineJobOutput<'import-ai-map'>> {
    const imported = await this.options.engine.importImage(
      await blobs.read(payload.imageHash, 16 * 1024 * 1024),
      payload,
      signal,
    );
    const facts = {
      width: imported.map.width,
      height: imported.map.height,
      teamCount: imported.map.teamCount,
    };
    if (
      facts.width !== payload.settings.width ||
      facts.height !== payload.settings.height ||
      facts.teamCount !== payload.settings.players
    )
      throw new EngineOutputError('Imported map settings mismatch');
    return {
      mapHash: await blobs.write(imported.bytes, CONTENT_TYPES.map),
      previewHash: await blobs.write(imported.png, CONTENT_TYPES.png),
      reportHash: await blobs.write(imported.json, CONTENT_TYPES.result),
      categoricalHash: await blobs.write(imported.tile, CONTENT_TYPES.png),
      size: imported.bytes.byteLength,
      previewWidth: pngSize(imported.png).width,
      previewHeight: pngSize(imported.png).height,
      map: facts,
    };
  }

  async generateMap(
    payload: EngineJobPayload<'generate-map'>,
    signal: AbortSignal,
    blobs: JobBlobs,
  ): Promise<EngineJobOutput<'generate-map'>> {
    const generated = await this.options.engine.generateMap(
      payload.generator,
      this.options.catalog,
      signal,
    );
    const problem = this.checkFacts(generated.map);
    if (problem) throw new EngineInputError(problem);
    if (generated.bytes.byteLength > this.limits.maxMapBytes) {
      throw new EngineInputError(
        `generated map is ${generated.bytes.byteLength} bytes; limit ${this.limits.maxMapBytes}`,
      );
    }
    const mapHash = await blobs.write(generated.bytes, CONTENT_TYPES.map, 'public');
    return {
      mapHash,
      size: generated.bytes.byteLength,
      map: generated.map,
      chosenSeed: generated.chosenSeed,
      ...(generated.startQuality ? { startQuality: generated.startQuality } : {}),
    };
  }

  async validateMap(
    payload: EngineJobPayload<'validate-map'>,
    signal: AbortSignal,
    blobs: JobBlobs,
  ): Promise<EngineJobOutput<'validate-map'>> {
    // Reasons are shown to players (map pages, the room's upload): plain words first.
    const kind = payload.format === 'save' ? 'save' : 'map';
    const what = payload.format === 'save' ? 'saved game' : 'map';
    const invalid = (reason: string) => ({ valid: false as const, reason: message(reason) });
    let stored: Uint8Array;
    try {
      stored = await blobs.read(payload.blobHash, this.limits.maxMapBytes);
    } catch (error) {
      if (error instanceof EngineInputError && !/not found/.test(error.message)) {
        return invalid(`This ${what} is too big to check. (${error.message})`);
      }
      throw error;
    }
    const check = checkMapFile(stored, {
      format: payload.format === 'save' ? 'save' : 'map',
      newestVersionMinor: this.options.catalog.versionMinor,
      maxBytes: this.limits.maxMapBytes,
    });
    if (!check.ok) return invalid(check.message);
    const { bytes, header } = check;
    let report;
    try {
      ({ report } = await this.options.engine.inspect(bytes, { signal }));
    } catch (error) {
      if (error instanceof EngineInputError)
        return invalid(
          `The game couldn't load this ${what}; the file may be damaged or incomplete. (Details: ${error.message})`,
        );
      throw error;
    }
    if (payload.format === 'map' && report.savedGame)
      return invalid(
        'This file is a saved game, not a map. Upload a map from the map editor instead.',
      );
    if (payload.format === 'save' && !report.savedGame)
      return invalid('This file is a map, not a saved game.');
    const problem = this.checkFacts(report);
    if (problem) return invalid(problem);
    const mapHash = await blobs.write(bytes, CONTENT_TYPES[kind]);
    const title = (report.name ?? header.name).trim().slice(0, 128);
    // Saves: who played, so a host can map returning players onto seats.
    const players =
      payload.format === 'save' ? savedPlayers(report.controllers, report.teamCount) : [];
    return {
      valid: true,
      mapHash,
      map: { width: report.width, height: report.height, teamCount: report.teamCount },
      versionMinor: header.versionMinor,
      ...(title ? { title } : {}),
      ...(players.length > 0 ? { players } : {}),
    };
  }

  async renderPreview(
    payload: EngineJobPayload<'render-preview'>,
    signal: AbortSignal,
    blobs: JobBlobs,
  ): Promise<EngineJobOutput<'render-preview'>> {
    if (payload.maxSizePx < PREVIEW_SIZE_RANGE.min) {
      throw new EngineInputError(
        `previews are rendered at ${PREVIEW_SIZE_RANGE.min} px or larger; scale smaller ones on the client`,
      );
    }
    const bytes = decompressIfGzip(
      await blobs.read(payload.mapHash, this.limits.maxMapBytes),
      this.limits.maxMapBytes,
    );
    const { png } = await this.options.engine.inspect(bytes, {
      previewSize: Math.min(payload.maxSizePx, PREVIEW_SIZE_RANGE.max),
      signal,
    });
    if (!png) throw new EngineOutputError('no preview written');
    const size = pngSize(png);
    const previewHash = await blobs.write(png, CONTENT_TYPES.png, 'public');
    return { previewHash, contentType: 'image/png', width: size.width, height: size.height };
  }

  async verifyMatch(
    payload: EngineJobPayload<'verify-match'>,
    signal: AbortSignal,
    blobs: JobBlobs,
  ): Promise<EngineJobOutput<'verify-match'>> {
    if (!sameSimVersion(payload.setup.simVersion, this.options.simVersion)) {
      throw new EngineInputError(
        `match setup is for ${simVersionKey(payload.setup.simVersion)}, this verifier runs ${simVersionKey(this.options.simVersion)}`,
      );
    }
    const record = await blobs.read(payload.recordHash, this.limits.maxRecordBytes);
    const map = decompressIfGzip(
      await blobs.read(payload.setup.map.hash, this.limits.maxMapBytes),
      this.limits.maxMapBytes,
    );
    const verification = await this.options.engine.verifyMatch(record, map, signal);
    const verdict = verification.verdict;
    // Seats whose orders the engine refused; the match page shows them.
    const rejections = verdict.orderRejections ? { orderRejections: verdict.orderRejections } : {};
    if (verdict.verdict === 'unverifiable') {
      return { verdict: 'unverifiable', reason: message(verdict.reason), ...rejections };
    }
    const { result, replay } = verification;
    if (!result || !replay) throw new EngineOutputError('verifier output incomplete');
    const [resultHash, replayHash] = [
      await blobs.write(result.json, CONTENT_TYPES.result, 'public'),
      await blobs.write(replay, CONTENT_TYPES.replay, 'public'),
    ];
    const outcome: VerifiedOutcome = {
      finalTick: result.game.finalTick,
      teams: result.game.teams.map((team) => ({
        team: team.team,
        outcome: team.outcome,
        ...(team.eliminatedTick !== undefined ? { eliminatedTick: team.eliminatedTick } : {}),
        prestige: team.prestige,
        statistics: team.statistics,
        timeline: team.timeline,
      })),
      resultHash,
      replayHash,
    };
    return verdict.verdict === 'verified'
      ? { verdict: 'verified', outcome, ...rejections }
      : { verdict: 'diverged', clients: verdict.seats, outcome, ...rejections };
  }
}

export { EngineCrashError };
