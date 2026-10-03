/** Hive Mind v1. Team-filtered read copies; all mutations use returned orders.
 * Synchronous step(ctx); no imports, network, filesystem, timers or promises.
 * Generated from the supported player API; runtime contract in commander-api.txt. */
export type Data = null | boolean | number | string | Data[] | {[key: string]: Data};

/** Unit/building namespaces are separate. New/reused slots and conversion get a
 * fresh generation; reset, upgrade and repair preserve identity. Deletion makes
 * lookup return null. Saves at format 125 and later preserve identity pairs;
 * released formats 58–124 receive identities through version-gated loading. */
export interface EntityRef { id: number; generation: number }
export type UnitType = 0 | 1 | 2; // worker, explorer, warrior
export type Priority = -1 | 0 | 1; // low, normal, high
export type ResourceId = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7;
export type BuildingName = 'swarm' | 'inn' | 'hospital' | 'racetrack' |
  'swimmingpool' | 'barracks' | 'school' | 'defencetower' | 'stonewall' | 'market';
export type FlagName = 'explorationflag' | 'warflag' | 'clearingflag';

export interface EntityFilter {
  /** Current team ID; omitted means all permitted teams. */
  team?: number | null;
  /** Visible results to skip, 0..32768; default 0. */
  offset?: number | null;
  /** Maximum results, 0..32768; default 32768. Large pages may exceed budgets. */
  limit?: number | null;
}
export interface Team {
  id: number; alive: boolean;
  /** Own-team only: bit t corresponds to allied team t. */
  allies?: number;
  /** Own-team only: 15 entries indexed by resource ID; 8..14 reserved. */
  resources?: number[];
}
export interface Unit extends EntityRef {
  team: number; type: UnitType; x: number; y: number; hp: number; maxHp: number;
  /** 17 entries, ability indices 0..16 (HP is 16); raw engine units. */
  levels: number[]; performance: number[];
  // All remaining fields are own-team only, including for allied units.
  experience?: number; experienceLevel?: number;
  /** Remaining food counter, not percent hungry. */
  hunger?: number;
  fruitCount?: number;
  /** Activity: random 0, filling 1, flag 2, upgrading 3. */
  activity?: 0 | 1 | 2 | 3;
  /** Engine movement code; see reference (10 unused). */
  movement?: number;
  /** Current ability ID. */
  action?: number;
  /** Medical: free 0, hungry 1, damaged 2. */
  medical?: 0 | 1 | 2;
  /** Resource ID, or -1 for nothing carried. */
  carriedResource?: number;
  /** Engine advancement units per tick; tile advancement wraps at 256. */
  speed?: number;
  /** NW 0, N 1, NE 2, E 3, SE 4, S 5, SW 6, W 7, none 8. */
  direction?: number;
  /** Negative inside a building; nonnegative outside. */
  insideTimeout?: number;
  /** Resource-ID bitmask of fruit kinds. */
  fruitMask?: number;
  /** Resource/ability purpose according to activity; -1 for none. */
  destinationPurpose?: number;
  /** Stored target-line coordinates; active-target validity is not exposed. */
  targetX?: number; targetY?: number;
  /** Visible referenced building, or null. */
  attachedBuilding?: EntityRef | null; targetBuilding?: EntityRef | null;
}
export interface Building extends EntityRef {
  team: number;
  /** Variant ID matched to BuildingType.id; shortType is the family code. */
  type: number; shortType: number;
  /** Anchor tile, not footprint center. */
  x: number; y: number;
  hp: number; maxHp: number; level: number; virtual: boolean;
  /** None 0, new building 1, upgrade 2, repair 3. */
  construction: 0 | 1 | 2 | 3;
  // All remaining fields are own-team only. Family-specific values are
  // present even where inapplicable; presence does not grant an order capability.
  /** Requested worker limits, not counts of workers actually present. */
  workers?: number; futureWorkers?: number;
  priority?: Priority;
  /** Flag radius in tiles. */
  range?: number;
  minimumLevel?: number;
  /** 15-entry resource arrays; stock may be shared team stock. */
  resources?: number[]; wishedResources?: number[];
  /** Relative swarm weights in worker/explorer/warrior order. */
  production?: [number, number, number];
  /** Swarm countdown in ticks. */
  productionTimeout?: number;
  /** Market resource-ID bitmasks. */
  receiveMask?: number; sendMask?: number;
  bullets?: number;
  /** Wood, wheat, papyrus, stone, alga. */
  clearingResources?: [boolean, boolean, boolean, boolean, boolean];
}
export interface TileResource {
  /** Wood 0, wheat 1, papyrus 2, stone 3, alga 4, cherry 5, orange 6, prune 7;
   * no resource 255 (with variety/amount zero). Presence is type !== 255,
   * since an eternal resource can have amount zero. */
  type: ResourceId | 255;
  variety: number; amount: number;
}
export interface Tile {
  x: number; y: number; visible: boolean; explored: boolean;
  // Unexplored tiles have only the four fields above.
  observedTick?: number;
  /** Raw graphic index: grass 0..15, sand 128..143, water 256..271;
   * other values are transitions. Not the C++ TerrainType enum. */
  terrain?: number;
  resource?: TileResource;
  /** Visible tiles only: bare occupant IDs, or 65535 for empty/hidden.
   * Find the matching entity list record to obtain its generation. */
  groundUnit?: number; airUnit?: number; building?: number;
  /** AI-only, explored tiles: current own-team forbidden flag. */
  forbidden?: boolean;
}
export interface BuildingType {
  /** Resolve by name/level/site; do not hardcode variant registry indices. */
  id: number; name: string; shortType: number; level: number; site: boolean;
  virtual: boolean; width: number; height: number; maxHp: number;
  maxWorkers: number;
  /** 15 configured resource capacities/costs. */
  resourceCapacity: number[];
}
export interface MapRead {
  width: number; height: number;
  /** Signed integer x/y in -32768..32767 wrap; result coordinates are canonical. */
  tile(x: number, y: number): Tile;
  /** Flat row-major result. Dimensions 0..256, subject to invocation budgets.
   * Exactly four arguments; zero dimension returns []. x/y wrap as in tile(). */
  region(x: number, y: number, width: number, height: number): Tile[];
}
export interface GameRead {
  teams(): Team[];
  /** Stable ascending team/slot order; offsets count visible results only. */
  units(filter?: EntityFilter): Unit[];
  buildings(filter?: EntityFilter): Building[];
  /** Hidden, dead, missing or stale => null; malformed IDs throw. */
  unit(reference: EntityRef): Unit | null;
  building(reference: EntityRef): Building | null;
  buildingTypes(): BuildingType[];
  map: MapRead;
}
export type Order =
  // Basic level-zero site or virtual flag. range is required for virtual flags.
  {type: 'create'; buildingType: number; x: number; y: number;
    workers: number; futureWorkers: number; range?: number} |
  // Worker fields: 0..20. Initial construction cancellation uses delete.
  {type: 'workers' | 'cancelConstruction'; building: EntityRef; workers: number} |
  {type: 'construction'; building: EntityRef; workers: number; futureWorkers: number} |
  {type: 'delete' | 'cancelDelete'; building: EntityRef} |
  {type: 'priority'; building: EntityRef; priority: Priority} |
  // Swarm only, three integers 0..16.
  {type: 'production'; building: EntityRef; ratios: [number, number, number]} |
  // Market only, masks 0..32767.
  {type: 'exchange'; building: EntityRef; receiveMask: number; sendMask: number} |
  // Flag only; radius 0..255, minimum level 0..3.
  {type: 'range'; building: EntityRef; range: number} |
  {type: 'minimumLevel'; building: EntityRef; level: number} |
  {type: 'moveFlag'; building: EntityRef; x: number; y: number} |
  // Clearing flag only; stone must be false.
  {type: 'clearingResources'; building: EntityRef; resources: [boolean, boolean, boolean, false, boolean]} |
  // Dimensions 1..256. Row-major mask anchored at x/y; 1 add, 2 remove.
  {type: 'forbidden' | 'guardArea' | 'clearArea'; x: number; y: number;
    width: number; height: number; mode: 1 | 2; mask: boolean[]};


export interface Context {
  tick: number;
  myTeam: number;
  random(): number;
  game: GameRead;
  wakeAgent(event: {key:string;reason:string;data?:Data}): void;
}
export interface Result {output?:Data;orders?:Order[]}
export type Step = (ctx:Context) => Result;
