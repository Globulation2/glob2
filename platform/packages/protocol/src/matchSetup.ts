// MatchSetup: the complete, engine-independent description of a match. One C++
// function turns it (plus the map bytes named by `map.hash`) into a GameHeader,
// for live clients and for the verifier alike, so the platform never has to
// parse or produce the engine's binary formats.
//
// Field names follow the GameHeader accessors they set (src/GameHeader.h). Every
// rule is required: a setup has exactly one meaning, with no defaults that two
// builds could disagree on.
import { Type, type Static } from 'typebox';
import {
  DisplayName,
  MAX_PLAYER_NAME_BYTES,
  MAX_TEAMS,
  SeatIndex,
  Sha256Hex,
  Strict,
  TeamIndex,
  Uint32,
  Uuid,
  utf8ByteLength,
} from './common.ts';
import { SimVersion } from './simVersion.ts';

export const MATCH_SETUP_SCHEMA_VERSION = 1;

/**
 * AI implementations by their CLI name (src/ai/AINames.cpp). The engine maps
 * these to AI::ImplementationID; `none` is AI::NONE (an inactive player).
 * JavaScript controllers are not yet supported online.
 */
export const AiId = Type.Union(
  [
    Type.Literal('none'),
    Type.Literal('numbi'),
    Type.Literal('castor'),
    Type.Literal('warrush'),
    Type.Literal('econo'),
    Type.Literal('nicowar'),
    Type.Literal('cortex'),
    Type.Literal('maxima'),
    Type.Literal('cabino'),
  ],
  { description: 'AI implementation, by its stable CLI name (src/ai/AINames.cpp).' },
);
export type AiId = Static<typeof AiId>;

/** A procedural map request. Generation runs in an engine-agent job for a sim version. */
export const GeneratorDescriptor = Strict(
  {
    generatorId: Type.String({
      pattern: '^[a-z0-9][a-z0-9._-]{0,63}$',
      description: 'GeneratorDefinition::id, e.g. "even-ground".',
    }),
    revision: Uint32,
    params: Type.Record(Type.String({ pattern: '^[A-Za-z0-9_-]{1,64}$' }), Type.Integer(), {
      description:
        'Every control value by GeneratorControl::id, including the shared "width" and "height" (log2 of the size), "teams" and "workers".',
    }),
    seed: Uint32,
    candidates: Type.Integer({
      minimum: 1,
      maximum: 64,
      description:
        'Number of seeded rolls; the best-scoring one is kept (GenerationService::bestSeed).',
    }),
    startingUnitLevel: Type.Integer({
      minimum: 0,
      maximum: 3,
      description: 'Level the generated starting workers spawn at (0 = standard).',
    }),
  },
  { description: 'Deterministic description of a generated map.' },
);
export type GeneratorDescriptor = Static<typeof GeneratorDescriptor>;

export const CatalogMapSource = Strict({
  kind: Type.Literal('catalog'),
  hash: Sha256Hex,
  mapId: Type.Optional(Uuid),
});

export const UploadedMapSource = Strict({
  kind: Type.Literal('upload'),
  format: Type.Union([Type.Literal('map'), Type.Literal('save')]),
  hash: Sha256Hex,
});

export const GeneratedMapSource = Strict({
  kind: Type.Literal('generated'),
  generator: GeneratorDescriptor,
  hash: Sha256Hex,
});

/**
 * The map a match is played on. `hash` is always the SHA-256 of the decompressed
 * bytes every client loads, whichever way the map was obtained.
 */
export const MapSource = Type.Union([CatalogMapSource, UploadedMapSource, GeneratedMapSource], {
  description: 'Map a match is played on; clients fetch the blob named by `hash`.',
});
export type MapSource = Static<typeof MapSource>;

export const SetupTeam = Strict(
  {
    team: TeamIndex,
    alliance: Type.Integer({
      minimum: 0,
      maximum: MAX_TEAMS - 1,
      description: 'Pre-game alliance group; GameHeader ally-team number is alliance + 1.',
    }),
  },
  { description: 'One colony of the map and its alliance group.' },
);
export type SetupTeam = Static<typeof SetupTeam>;

export const HumanSeat = Strict({
  seat: SeatIndex,
  kind: Type.Literal('human'),
  team: TeamIndex,
  name: DisplayName,
  accountId: Type.Optional(Uuid),
});

export const AiSeat = Strict({
  seat: SeatIndex,
  kind: Type.Literal('ai'),
  team: TeamIndex,
  name: DisplayName,
  ai: AiId,
  aiConfig: Type.Optional(
    Type.String({
      maxLength: 4096,
      description:
        'Canonical resolved runtime values (GameHeader::setAIConfig); omitted = default.',
    }),
  ),
});

/**
 * A closed team: a room's empty or locked seat. It is not a player. Its colony is
 * removed at the start, exactly like a "Closed" colony in a custom game, so it
 * has lost from the first step and never blocks a victory. Closed seats follow
 * every human and AI seat (players keep the numbers 0..p-1) and each closes a
 * different team that no player seat controls. A team that no seat names at all
 * is closed in the same way; a closed seat states it explicitly.
 */
export const ClosedSeat = Strict({
  seat: SeatIndex,
  kind: Type.Literal('closed'),
  team: TeamIndex,
});

/**
 * A human or AI seat is a player record (BasePlayer number = seat). Several
 * seats may share a team: a human plus an AI on one team is the custom-game
 * "shared control" mode, and several humans may control one team together.
 * AI `none` is an idle player whose colony stays on the map; rooms send empty
 * seats as `closed` instead.
 */
export const Seat = Type.Union([HumanSeat, AiSeat, ClosedSeat]);
export type Seat = Static<typeof Seat>;

export const MatchRules = Strict(
  {
    prestigeVictory: Type.Boolean({ description: 'Prestige winning condition enabled.' }),
    suddenDeathMinutes: Type.Integer({
      minimum: 0,
      maximum: 1440,
      description: 'Sudden-death timer in game minutes; 0 disables it.',
    }),
    mapDiscovered: Type.Boolean(),
    allyTeamsFixed: Type.Boolean({ description: 'Alliances cannot change during the game.' }),
    resourceGrowthDisabled: Type.Boolean(),
    resourceScarcityLevel: Type.Integer({ minimum: 0, maximum: 3 }),
    instantConstruction: Type.Boolean(),
    stockpileStartLevel: Type.Integer({ minimum: 0, maximum: 3 }),
    hungerDisabled: Type.Boolean(),
    unitUpgradesDisabled: Type.Boolean(),
    glassCannonLevel: Type.Integer({ minimum: 0, maximum: 2 }),
    unitsFearless: Type.Boolean(),
    permadeathDisabled: Type.Boolean(),
    peacefulMode: Type.Boolean(),
    buildingHpLevel: Type.Integer({ minimum: 0, maximum: 2 }),
  },
  { description: 'Custom-game rules, named after the GameHeader accessors they set.' },
);
export type MatchRules = Static<typeof MatchRules>;

export const MatchSetup = Strict(
  {
    schemaVersion: Type.Literal(MATCH_SETUP_SCHEMA_VERSION),
    simVersion: SimVersion,
    seed: Type.Integer({
      minimum: 0,
      maximum: 4294967295,
      description: 'GameHeader random seed, chosen by the platform.',
    }),
    map: MapSource,
    teams: Type.Array(SetupTeam, {
      minItems: 1,
      maxItems: MAX_TEAMS,
      description: 'Every team of the map, in order 0..n-1 (n must equal the map team count).',
    }),
    seats: Type.Array(Seat, {
      minItems: 1,
      maxItems: MAX_TEAMS,
      description:
        'Player records (human and AI seats) in BasePlayer number order 0..p-1, then any closed seats.',
    }),
    rules: MatchRules,
    experiments: Type.Array(Type.String({ pattern: '^[a-z0-9]+(-[a-z0-9]+)*$', maxLength: 64 }), {
      maxItems: 64,
      uniqueItems: true,
      description:
        'Experimental-feature keys (ExperimentalFeatures.cpp); unknown keys are an error.',
    }),
  },
  { description: 'Complete engine-independent description of a match.' },
);
export type MatchSetup = Static<typeof MatchSetup>;

/** Standard rules: the custom-game "Standard" preset. */
export const STANDARD_RULES: MatchRules = {
  prestigeVictory: true,
  suddenDeathMinutes: 0,
  mapDiscovered: false,
  allyTeamsFixed: true,
  resourceGrowthDisabled: false,
  resourceScarcityLevel: 0,
  instantConstruction: false,
  stockpileStartLevel: 0,
  hungerDisabled: false,
  unitUpgradesDisabled: false,
  glassCannonLevel: 0,
  unitsFearless: false,
  permadeathDisabled: false,
  peacefulMode: false,
  buildingHpLevel: 0,
};

export interface SetupProblem {
  path: string;
  message: string;
}

/**
 * Checks the cross-field rules JSON Schema cannot express. A setup is valid only
 * if it passes both the MatchSetup schema and this function; the C++ converter
 * enforces the same rules (plus the map's real team count).
 */
export function matchSetupProblems(setup: MatchSetup): SetupProblem[] {
  const problems: SetupProblem[] = [];
  setup.teams.forEach((team, index) => {
    if (team.team !== index) {
      problems.push({
        path: `/teams/${index}/team`,
        message: `teams must list team indices 0..n-1 in order; expected ${index}`,
      });
    }
  });
  const teamCount = setup.teams.length;
  setup.seats.forEach((seat, index) => {
    if (seat.seat !== index) {
      problems.push({
        path: `/seats/${index}/seat`,
        message: `seats must be numbered 0..k-1 in order; expected ${index}`,
      });
    }
    if (seat.team >= teamCount) {
      problems.push({
        path: `/seats/${index}/team`,
        message: `team ${seat.team} is not one of the ${teamCount} teams`,
      });
    }
    if (seat.kind !== 'closed' && utf8ByteLength(seat.name) > MAX_PLAYER_NAME_BYTES) {
      problems.push({
        path: `/seats/${index}/name`,
        message: `name exceeds ${MAX_PLAYER_NAME_BYTES} UTF-8 bytes`,
      });
    }
  });
  const humanAccounts = new Set<string>();
  setup.seats.forEach((seat, index) => {
    if (seat.kind === 'human' && seat.accountId !== undefined) {
      if (humanAccounts.has(seat.accountId)) {
        problems.push({
          path: `/seats/${index}/accountId`,
          message: 'an account may hold only one seat',
        });
      }
      humanAccounts.add(seat.accountId);
    }
  });
  // Closed seats follow the players and each closes a team no player controls.
  const players = playerSeats(setup);
  const playedTeams = new Set(players.map((seat) => seat.team));
  if (players.length === 0) {
    problems.push({ path: '/seats', message: 'a match needs at least one human or AI seat' });
  }
  const closedTeams = new Set<number>();
  setup.seats.forEach((seat, index) => {
    if (seat.kind !== 'closed') {
      if (index >= players.length) {
        problems.push({
          path: `/seats/${index}`,
          message: 'closed seats must come after every human and AI seat',
        });
      }
      return;
    }
    if (playedTeams.has(seat.team)) {
      problems.push({
        path: `/seats/${index}/team`,
        message: `team ${seat.team} is played by another seat`,
      });
    } else if (closedTeams.has(seat.team)) {
      problems.push({ path: `/seats/${index}/team`, message: `team ${seat.team} is closed twice` });
    }
    closedTeams.add(seat.team);
  });
  if (setup.map.kind === 'generated') {
    const teams = setup.map.generator.params['teams'];
    if (teams !== undefined && teams !== teamCount) {
      problems.push({
        path: '/map/generator/params/teams',
        message: `generator teams (${teams}) must equal the number of setup teams (${teamCount})`,
      });
    }
  }
  return problems;
}

export type CatalogMapSource = Static<typeof CatalogMapSource>;
export type UploadedMapSource = Static<typeof UploadedMapSource>;
export type GeneratedMapSource = Static<typeof GeneratedMapSource>;
export type HumanSeat = Static<typeof HumanSeat>;
export type AiSeat = Static<typeof AiSeat>;
export type ClosedSeat = Static<typeof ClosedSeat>;
/** A human or AI seat: a player. */
export type PlayerSeat = HumanSeat | AiSeat;

/** The human and AI seats of a setup (BasePlayer records 0..p-1), in seat order. */
export function playerSeats(setup: { seats: readonly Seat[] }): PlayerSeat[] {
  return setup.seats.filter((seat): seat is PlayerSeat => seat.kind !== 'closed');
}
