// Everything the agent knows about the glob2 command line: the arguments of
// each headless command and the shape of the files it writes. This is the one
// module that reads engine output formats, so a change in the engine's CLI or
// result files is a change here only. Functions are pure; engine.ts spawns.
//
// Commands used (see docs/tools/tournaments.md and docs/map-generators/CLI.md):
//   --headless-catalog                       existing; JSON on stdout
//   --sim-version                            ASSUMED (being added on the engine
//                                            integration branch): JSON on stdout
//   --generate-map --output-dir … (structured)  existing; result.json + map-r0.map.gz
//   --preview-map <file> --json … --output … existing; map report + PNG
//   --verify-match <record> --map <file> --out <dir>
//                                            ASSUMED (being built in M1); see
//                                            parseVerifyOutputs for the contract
import type {
  BuildingCatalog,
  ResourceExperimentDefinitions,
  GeneratorDescriptor,
  SimVersion,
  TeamTimelinePoint,
} from '@glob2/protocol';
import {
  MAX_TIMELINE_SAMPLES,
  MapSetCredits,
  parse,
  buildingCatalogExperimentKeys,
  resourceExperimentKeys,
} from '@glob2/protocol';
import { checkBuildingCatalogHash } from '@glob2/protocol/node';

/** A failure caused by the job's input: deterministic, so it is reported, not retried. */
export class EngineInputError extends Error {
  override name = 'EngineInputError';
}

/** The engine produced output that breaks the contract this module expects. */
export class EngineOutputError extends Error {
  override name = 'EngineOutputError';
}

type Json = Record<string, unknown>;

function isObject(value: unknown): value is Json {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function int(value: unknown, what: string): number {
  if (typeof value !== 'number' || !Number.isInteger(value)) {
    throw new EngineOutputError(`${what} is not an integer`);
  }
  return value;
}

function object(value: unknown, what: string): Json {
  if (!isObject(value)) throw new EngineOutputError(`${what} is not an object`);
  return value;
}

export function parseJson(text: string, what: string): unknown {
  try {
    return JSON.parse(text) as unknown;
  } catch (error) {
    throw new EngineOutputError(`${what} is not JSON: ${(error as Error).message}`);
  }
}

// ---------------------------------------------------------------- catalog

export interface CatalogControl {
  id: string;
  values: number[];
}

export interface CatalogGenerator {
  /** Numeric method id the structured CLI takes. */
  method: number;
  id: string;
  revision: number;
  editorOnly: boolean;
  controls: Map<string, CatalogControl>;
}

export interface EngineCatalog {
  buildingCatalogHash?: string;
  /** VERSION_MINOR of the binary (`save_version`). */
  versionMinor: number;
  /** NET_PROTOCOL_VERSION of the binary (`protocol_version`). */
  netProtocol: number;
  /** Structured commands the binary advertises (`commands`). */
  commands: string[];
  /** Simulation data hash, if the catalog carries one (`data_hash`; ASSUMED future field). */
  dataHash?: string;
  generators: Map<string, CatalogGenerator>;
}

export const CATALOG_ARGS = ['--headless-catalog'] as const;

/** Parses `glob2 --headless-catalog` (schema_version 1). */
export function parseCatalog(stdout: string): EngineCatalog {
  const doc = object(parseJson(stdout, 'headless catalog'), 'headless catalog');
  if (doc['schema_version'] !== 1) {
    throw new EngineOutputError(
      `unsupported catalog schema_version ${String(doc['schema_version'])}`,
    );
  }
  const generators = new Map<string, CatalogGenerator>();
  const list = doc['generators'];
  if (!Array.isArray(list)) throw new EngineOutputError('catalog has no generators list');
  for (const raw of list) {
    const g = object(raw, 'catalog generator');
    if (typeof g['id'] !== 'string') throw new EngineOutputError('catalog generator without id');
    const controls = new Map<string, CatalogControl>();
    for (const c of Array.isArray(g['controls']) ? g['controls'] : []) {
      const control = object(c, 'generator control');
      if (typeof control['id'] !== 'string' || !Array.isArray(control['values'])) continue;
      controls.set(control['id'], {
        id: control['id'],
        values: control['values'].filter((v): v is number => typeof v === 'number'),
      });
    }
    generators.set(g['id'], {
      method: int(g['method'], 'generator method'),
      id: g['id'],
      revision: int(g['revision'], 'generator revision'),
      editorOnly: g['editorOnly'] === true,
      controls,
    });
  }
  const catalog: EngineCatalog = {
    versionMinor: int(doc['save_version'], 'catalog save_version'),
    netProtocol: int(doc['protocol_version'], 'catalog protocol_version'),
    commands: Array.isArray(doc['commands'])
      ? doc['commands'].filter((c): c is string => typeof c === 'string')
      : [],
    generators,
  };
  const buildingCatalogHash = doc['building_catalog_hash'];
  if (typeof buildingCatalogHash === 'string' && /^[0-9a-f]{64}$/.test(buildingCatalogHash))
    catalog.buildingCatalogHash = buildingCatalogHash;
  const dataHash = doc['data_hash'];
  if (typeof dataHash === 'string' && /^[0-9a-f]{64}$/.test(dataHash)) catalog.dataHash = dataHash;
  return catalog;
}

// ------------------------------------------------------------ sim version

export const SIM_VERSION_ARGS = ['--sim-version'] as const;

/**
 * Whether to run `--sim-version`. A binary without the flag does not reject
 * it: it falls through to the game's start-up and opens the menu, so the agent
 * only probes when the catalog advertises the command (ASSUMED name
 * "sim_version" in `commands`) or the operator forces the probe.
 */
export function supportsSimVersionFlag(catalog: EngineCatalog): boolean {
  return catalog.commands.includes('sim_version');
}

/**
 * Parses the ASSUMED `glob2 --sim-version` output: one JSON object on stdout,
 * `{"versionMinor":125,"netProtocol":49,"dataHash":"<64 hex>"}` (snake_case
 * keys `version_minor`, `net_protocol`, `data_hash` are accepted too). Returns
 * undefined when the output is not such a document (an older binary that
 * lacks the flag starts its GUI path or prints usage instead).
 */
export function parseSimVersionOutput(stdout: string): SimVersion | undefined {
  const line = stdout
    .split('\n')
    .map((l) => l.trim())
    .reverse()
    .find((l) => l.startsWith('{'));
  if (!line) return undefined;
  let doc: unknown;
  try {
    doc = JSON.parse(line);
  } catch {
    return undefined;
  }
  if (!isObject(doc)) return undefined;
  const minor = doc['versionMinor'] ?? doc['version_minor'];
  const net = doc['netProtocol'] ?? doc['net_protocol'];
  const hash = doc['dataHash'] ?? doc['data_hash'];
  if (
    typeof minor !== 'number' ||
    typeof net !== 'number' ||
    typeof hash !== 'string' ||
    !/^[0-9a-f]{64}$/.test(hash)
  ) {
    return undefined;
  }
  return { versionMinor: minor, netProtocol: net, dataHash: hash };
}

// ----------------------------------------------------------- generate-map

/**
 * Arguments for the structured generator. The descriptor is checked against
 * the binary's catalog first, so an unknown generator, a revision this binary
 * does not produce, or a parameter value outside the registered values is an
 * input error rather than whatever the engine would make of it.
 */
export function generateMapArgs(
  descriptor: GeneratorDescriptor,
  catalog: EngineCatalog,
  outputDir: string,
): string[] {
  const generator = catalog.generators.get(descriptor.generatorId);
  if (!generator || generator.editorOnly) {
    throw new EngineInputError(
      `generator ${descriptor.generatorId} is not available in this engine`,
    );
  }
  if (generator.revision !== descriptor.revision) {
    throw new EngineInputError(
      `generator ${descriptor.generatorId} is at revision ${generator.revision} in this engine, not ${descriptor.revision}`,
    );
  }
  if (descriptor.startingUnitLevel !== 0) {
    throw new EngineInputError(
      'startingUnitLevel other than 0 is not supported by the structured generator command',
    );
  }
  const args = [
    '--generate-map',
    '--generator',
    String(generator.method),
    '--map-seed',
    String(descriptor.seed),
    '--candidates',
    String(descriptor.candidates),
    '--write-map',
    'true',
  ];
  const params = Object.entries(descriptor.params).sort(([a], [b]) => (a < b ? -1 : 1));
  for (const [key, value] of params) {
    const control = generator.controls.get(key);
    if (!control) {
      throw new EngineInputError(`generator ${descriptor.generatorId} has no parameter ${key}`);
    }
    if (control.values.length > 0 && !control.values.includes(value)) {
      throw new EngineInputError(
        `generator ${descriptor.generatorId}: ${key}=${value} is not one of ${control.values.join(', ')}`,
      );
    }
    args.push('--param', `${key}=${value}`);
  }
  args.push('--output-dir', outputDir);
  return args;
}

/** File the structured generator writes the chosen map to (gzip). */
export const GENERATED_MAP_FILE = 'map-r0.map.gz';

export interface MapFacts {
  setCredits?: MapSetCredits;
  buildingCatalog?: BuildingCatalog;
  resourceExperiments?: ResourceExperimentDefinitions;
  requiredResourceExperiments?: string[];
  width: number;
  height: number;
  teamCount: number;
}

export interface GenerationOutcome {
  chosenSeed: number;
  map: MapFacts;
  startQuality?: { fairness: number; score: number };
}

/**
 * Reads the structured generator's result.json. A result with a status other
 * than "completed" is the generator refusing the request (an input error).
 */
export function parseGenerationResult(text: string): GenerationOutcome {
  const doc = object(parseJson(text, 'generation result'), 'generation result');
  if (doc['status'] !== 'completed') {
    const diagnostic =
      typeof doc['diagnostic'] === 'string' ? doc['diagnostic'] : JSON.stringify(doc['status']);
    throw new EngineInputError(`generation failed (${String(doc['status'])}): ${diagnostic}`);
  }
  const report = object(doc['map_report'], 'generation map_report');
  const map = parseReportMap(report);
  const outcome: GenerationOutcome = {
    chosenSeed: int(doc['chosen_seed'], 'chosen_seed'),
    map: {
      width: map.width,
      height: map.height,
      teamCount: map.teamCount,
      ...(map.buildingCatalog ? { buildingCatalog: map.buildingCatalog } : {}),
      ...(map.setCredits ? { setCredits: map.setCredits } : {}),
      ...(map.resourceExperiments ? { resourceExperiments: map.resourceExperiments } : {}),
      ...(map.requiredResourceExperiments
        ? { requiredResourceExperiments: map.requiredResourceExperiments }
        : {}),
    },
  };
  const quality = doc['quality'];
  if (
    isObject(quality) &&
    typeof quality['fairness'] === 'number' &&
    typeof quality['score'] === 'number'
  ) {
    outcome.startQuality = { fairness: quality['fairness'], score: quality['score'] };
  }
  return outcome;
}

// ---------------------------------------------------- preview / map report

/** Arguments that load a map or save with the game's loader, writing a report and optionally a PNG. */
export function previewMapArgs(
  inputPath: string,
  options: { reportPath?: string; pngPath?: string; previewSize?: number },
): string[] {
  const args = ['--preview-map', inputPath];
  if (options.pngPath) {
    args.push('--output', options.pngPath);
    if (options.previewSize) args.push('--preview-size', String(options.previewSize));
  }
  if (options.reportPath) args.push('--json', options.reportPath);
  return args;
}

/** The engine's --preview-map size range (MapCommand.cpp). */
export const PREVIEW_SIZE_RANGE = { min: 128, max: 4096 } as const;

/** One `map.controllers[]` entry: a player slot of the file's game header. */
export interface ReportController {
  slot: number;
  team: number;
  /** BasePlayer::PlayerType: 0 none, 1 dropping, 2 lost, 3 network, 4 local, 5 + n AI n. */
  type: number;
  /** Stored player name; engines before the field was added report none. */
  name?: string;
}

export interface ReportMap extends MapFacts {
  name: string | null;
  savedGame: boolean;
  tick: number;
  controllers: ReportController[];
}

function parseControllers(value: unknown): ReportController[] {
  if (!Array.isArray(value)) return [];
  return value.map((entry, i) => {
    const c = object(entry, `map controller ${i}`);
    return {
      slot: int(c['slot'], 'controller slot'),
      team: int(c['team'], 'controller team'),
      type: int(c['type'], 'controller type'),
      ...(typeof c['name'] === 'string' ? { name: c['name'] } : {}),
    };
  });
}

function parseReportMap(report: Json): ReportMap {
  const map = object(report['map'], 'map report map');
  let buildingCatalog: BuildingCatalog | undefined;
  let resourceExperiments: ResourceExperimentDefinitions | undefined;
  let requiredResourceExperiments: string[] | undefined;
  if (map['resourceExperiments'] !== undefined) {
    resourceExperiments = map['resourceExperiments'] as ResourceExperimentDefinitions;
    try {
      resourceExperimentKeys(resourceExperiments);
    } catch (error) {
      throw new EngineOutputError(String(error));
    }
  }
  if (map['requiredResourceExperiments'] !== undefined) {
    const required = map['requiredResourceExperiments'];
    const allowed = new Set(resourceExperimentKeys(resourceExperiments ?? []));
    if (
      !Array.isArray(required) ||
      required.length > 64 ||
      required.some((key: unknown) => typeof key !== 'string' || !allowed.has(key)) ||
      new Set(required).size !== required.length
    )
      throw new EngineOutputError('invalid required resource experiments');
    requiredResourceExperiments = required as string[];
  }
  if (map['buildingCatalog'] !== undefined) {
    const catalog = object(map['buildingCatalog'], 'building catalog');
    if (typeof catalog['snapshot'] !== 'string' || typeof catalog['hash'] !== 'string')
      throw new EngineOutputError('invalid building catalog');
    buildingCatalog = { snapshot: catalog['snapshot'], hash: catalog['hash'] };
    try {
      buildingCatalogExperimentKeys(buildingCatalog);
      checkBuildingCatalogHash(buildingCatalog);
    } catch (error) {
      throw new EngineOutputError(String(error));
    }
  }
  return {
    ...(buildingCatalog ? { buildingCatalog } : {}),
    ...(map['setCredits'] !== undefined
      ? { setCredits: parse(MapSetCredits, map['setCredits'], 'map set credits') }
      : {}),
    ...(resourceExperiments ? { resourceExperiments } : {}),
    ...(requiredResourceExperiments ? { requiredResourceExperiments } : {}),
    name: typeof map['name'] === 'string' ? map['name'] : null,
    width: int(map['width'], 'map width'),
    height: int(map['height'], 'map height'),
    teamCount: int(map['player_slots'], 'map player_slots'),
    savedGame: map['saved_game'] === true,
    tick: typeof map['tick'] === 'number' ? map['tick'] : 0,
    controllers: parseControllers(map['controllers']),
  };
}

/** BasePlayer::P_NONE and P_AI (the first AI type). */
const PLAYER_NONE = 0;
const PLAYER_AI = 5;

export interface SavedPlayerFacts {
  name: string;
  team: number;
  kind: 'human' | 'ai';
}

/**
 * The players a saved game was saved with (ValidateMapResult.players), from
 * its report's controllers. Names come from the file when the engine reports
 * them (multiplayer/map-report-players), else "Player N" / "AI N" by slot.
 */
export function savedPlayers(
  controllers: readonly ReportController[],
  teamCount: number,
): SavedPlayerFacts[] {
  return controllers
    .filter((c) => c.type !== PLAYER_NONE && c.team >= 0 && c.team < Math.min(teamCount, 12))
    .slice(0, 12)
    .map((c) => {
      const kind = c.type >= PLAYER_AI ? 'ai' : 'human';
      const stored = Array.from((c.name ?? '').trim())
        .slice(0, 64)
        .join('');
      return {
        name: stored || `${kind === 'ai' ? 'AI' : 'Player'} ${c.slot + 1}`,
        team: c.team,
        kind,
      };
    });
}

/** Reads a map report (docs/map-generators/REPORT.md, schema_version 2). */
export function parseMapReport(text: string): ReportMap {
  const report = object(parseJson(text, 'map report'), 'map report');
  if (report['schema_version'] !== 2) {
    throw new EngineOutputError(
      `unsupported map report schema_version ${String(report['schema_version'])}`,
    );
  }
  return parseReportMap(report);
}

// ------------------------------------------------------------ verify-match

export function verifyMatchArgs(recordPath: string, mapPath: string, outputDir: string): string[] {
  return ['--verify-match', recordPath, '--map', mapPath, '--out', outputDir];
}

/** Files --verify-match writes into its --out directory (assumed contract). */
export const VERIFY_FILES = {
  verdict: 'verdict.json',
  result: 'result.json',
  replay: 'match.replay',
} as const;

/** Final team counters copied from result.json (`teams[].statistics` and the unit/building counts). */
export type TeamStatistics = Record<string, number>;

export interface GameTeamResult {
  team: number;
  outcome: 'won' | 'lost' | 'unresolved';
  eliminatedTick?: number;
  prestige: number;
  statistics: TeamStatistics;
  timeline: TeamTimelinePoint[];
}

export interface GameResult {
  finalTick: number;
  teams: GameTeamResult[];
}

/** Engine end-of-game statistics are sampled every 512 ticks (TeamStat.cpp). */
export const TIMELINE_INTERVAL_TICKS = 512;

const COUNTERS: [string, string][] = [
  ['units', 'units'],
  ['workers', 'workers'],
  ['explorers', 'explorers'],
  ['warriors', 'warriors'],
  ['warrior_hp', 'warriorHp'],
  ['warrior_attack', 'warriorAttack'],
  ['buildings', 'buildings'],
  ['sites', 'sites'],
];
const STATISTICS: [string, string][] = [
  ['total_units', 'totalUnits'],
  ['total_buildings', 'totalBuildings'],
  ['total_hp', 'totalHp'],
  ['total_attack_power', 'totalAttackPower'],
  ['total_defense_power', 'totalDefensePower'],
  ['food', 'food'],
  ['food_capacity', 'foodCapacity'],
  ['need_food', 'needFood'],
];

/**
 * Reads a HeadlessRunner game result.json (job_type "game"): per team the
 * outcome, prestige, elimination tick (-1 = never), counters and `history`,
 * the end-of-game statistics sampled every 512 ticks as
 * [units, buildings, prestige, hp, attack, defense].
 */
export function parseGameResult(text: string): GameResult {
  const doc = object(parseJson(text, 'game result'), 'game result');
  if (doc['schema_version'] !== 1) {
    throw new EngineOutputError(
      `unsupported result schema_version ${String(doc['schema_version'])}`,
    );
  }
  if (doc['status'] !== 'completed') {
    throw new EngineOutputError(
      `result status ${String(doc['status'])}: ${String(doc['diagnostic'] ?? '')}`,
    );
  }
  if (!Array.isArray(doc['teams'])) throw new EngineOutputError('result has no teams');
  const teams = doc['teams'].map((raw): GameTeamResult => {
    const team = object(raw, 'result team');
    const outcome = team['outcome'];
    if (outcome !== 'won' && outcome !== 'lost' && outcome !== 'unresolved') {
      throw new EngineOutputError(`team outcome ${String(outcome)}`);
    }
    const statistics: TeamStatistics = {};
    for (const [from, to] of COUNTERS) {
      if (typeof team[from] === 'number') statistics[to] = team[from];
    }
    const stats = isObject(team['statistics']) ? team['statistics'] : {};
    for (const [from, to] of STATISTICS) {
      if (typeof stats[from] === 'number') statistics[to] = stats[from];
    }
    statistics['alive'] = team['alive'] === false ? 0 : 1;
    const history = Array.isArray(team['history']) ? team['history'] : [];
    const timeline = history.slice(-MAX_TIMELINE_SAMPLES).map((sample, index) => {
      if (!Array.isArray(sample) || sample.length < 6) {
        throw new EngineOutputError(
          'team history sample is not [units, buildings, prestige, hp, attack, defense]',
        );
      }
      const [units, buildings, prestige, hp, attack, defense] = sample.map((v) =>
        int(v, 'history value'),
      ) as [number, number, number, number, number, number];
      const offset = history.length - Math.min(history.length, MAX_TIMELINE_SAMPLES);
      return {
        tick: (index + offset) * TIMELINE_INTERVAL_TICKS,
        units,
        buildings,
        prestige,
        hp,
        attack,
        defense,
      };
    });
    const eliminated = int(team['eliminated_tick'] ?? -1, 'eliminated_tick');
    return {
      team: int(team['team'], 'team'),
      outcome,
      ...(eliminated >= 0 ? { eliminatedTick: eliminated } : {}),
      prestige: int(team['prestige'] ?? 0, 'prestige'),
      statistics,
      timeline,
    };
  });
  return { finalTick: int(doc['ticks'], 'ticks'), teams };
}

/**
 * A seat whose sequenced orders the verifier refused (verdict.json
 * `orderRejections`, see docs/multiplayer/turn-protocol.md): counts by reason.
 */
export interface VerifierOrderRejection {
  seat: number;
  rejected: number;
  stale: number;
  reasons: Record<string, number>;
  firstRejectedTick?: number;
}

export type VerifierVerdict = (
  | { verdict: 'verified' }
  | { verdict: 'diverged'; seats: number[] }
  | { verdict: 'unverifiable'; reason: string }
) & { orderRejections?: VerifierOrderRejection[] };

/** Well-formed entries of verdict.json `orderRejections`; anything else is dropped. */
function parseOrderRejections(value: unknown): VerifierOrderRejection[] | undefined {
  if (!Array.isArray(value)) return undefined;
  const count = (v: unknown) =>
    typeof v === 'number' && Number.isFinite(v) && v >= 0 ? Math.trunc(v) : 0;
  const rejections: VerifierOrderRejection[] = [];
  for (const item of value) {
    if (!item || typeof item !== 'object') continue;
    const r = item as Record<string, unknown>;
    const seat = r['seat'];
    if (typeof seat !== 'number' || !Number.isInteger(seat) || seat < 0 || seat > 11) continue;
    const reasons: Record<string, number> = {};
    if (r['reasons'] && typeof r['reasons'] === 'object') {
      for (const [key, n] of Object.entries(r['reasons'] as Record<string, unknown>)) {
        if (typeof n === 'number') reasons[key.slice(0, 64)] = count(n);
      }
    }
    rejections.push({
      seat,
      rejected: count(r['rejected']),
      stale: count(r['stale']),
      reasons,
      ...(typeof r['firstRejectedTick'] === 'number'
        ? { firstRejectedTick: count(r['firstRejectedTick']) }
        : {}),
    });
  }
  return rejections;
}

/**
 * Reads `<out>/verdict.json`. ASSUMED contract (the command is being built on
 * the engine integration branch and may still change):
 *   {"verdict":"verified"|"diverged"|"unverifiable","seats":[…]?,"reason":"…"?}
 * `seats` lists the diverged clients' seats; `reason` explains an
 * unverifiable verdict. The process exit code is not trusted to carry the
 * verdict: a verdict.json, when present, decides. Without one, exit code 2
 * means the request was invalid (as for the other structured commands) and
 * anything else is an engine failure.
 */
export function parseVerdict(text: string): VerifierVerdict {
  const doc = object(parseJson(text, 'verdict'), 'verdict');
  const rejections = parseOrderRejections(doc['orderRejections']);
  const extra = rejections && rejections.length > 0 ? { orderRejections: rejections } : {};
  return { ...parseVerdictKind(doc), ...extra };
}

function parseVerdictKind(doc: Record<string, unknown>): VerifierVerdict {
  switch (doc['verdict']) {
    case 'verified':
      return { verdict: 'verified' };
    case 'diverged': {
      const seats = doc['seats'] ?? doc['clients'];
      if (!Array.isArray(seats) || seats.length === 0) {
        throw new EngineOutputError('diverged verdict without seats');
      }
      return {
        verdict: 'diverged',
        seats: [...new Set(seats.map((s) => int(s, 'diverged seat')))].sort((a, b) => a - b),
      };
    }
    case 'unverifiable':
      return {
        verdict: 'unverifiable',
        reason: typeof doc['reason'] === 'string' && doc['reason'] ? doc['reason'] : 'unverifiable',
      };
    default:
      throw new EngineOutputError(`unknown verdict ${JSON.stringify(doc['verdict'])}`);
  }
}

// ------------------------------------------------------------- map header

// The header reader lives in @glob2/core (shared with the API's upload checks).
export { readMapHeader, type MapHeader } from '@glob2/core';

export interface BuildingCompositionResult {
  schemaVersion: 1;
  baseHash: string;
  catalog: BuildingCatalog;
  artworkHash?: string;
}

/** The engine owns defaults, dense IDs and semantic validation. */
export function parseBuildingComposition(
  text: string,
  expectedBaseHash: string,
  expectedArtworkHash?: string,
): BuildingCompositionResult {
  const doc = object(parseJson(text, 'building composition'), 'building composition');
  if (doc['schemaVersion'] !== 1 || doc['baseHash'] !== expectedBaseHash)
    throw new EngineOutputError('building composition used an unexpected base catalog');
  const catalog = object(doc['catalog'], 'composed building catalog');
  if (
    typeof catalog['snapshot'] !== 'string' ||
    !catalog['snapshot'] ||
    Buffer.byteLength(catalog['snapshot']) > 8 * 1024 * 1024 ||
    typeof catalog['hash'] !== 'string' ||
    !/^[0-9a-f]{64}$/.test(catalog['hash'])
  )
    throw new EngineOutputError('invalid composed building catalog');
  const result: BuildingCatalog = { snapshot: catalog['snapshot'], hash: catalog['hash'] };
  try {
    checkBuildingCatalogHash(result);
    buildingCatalogExperimentKeys(result);
  } catch (error) {
    throw new EngineOutputError(`invalid building composition: ${String(error)}`);
  }
  const artworkHash = doc['artworkHash'];
  if (
    artworkHash !== undefined &&
    (typeof artworkHash !== 'string' || !/^[0-9a-f]{64}$/.test(artworkHash))
  )
    throw new EngineOutputError('invalid checked artwork hash');
  if (expectedArtworkHash !== undefined && artworkHash !== expectedArtworkHash)
    throw new EngineOutputError('building composition did not check the expected artwork');
  return {
    schemaVersion: 1,
    baseHash: expectedBaseHash,
    catalog: result,
    ...(typeof artworkHash === 'string' ? { artworkHash } : {}),
  };
}
