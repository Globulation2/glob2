/** Hive Mind v1. Team-filtered read copies; all mutations use returned orders.
 * Synchronous step(ctx) (main(ctx) is accepted as another name); no imports, network,
 * filesystem, timers or promises. A run converts at most 1 MiB of strings and record keys
 * from JavaScript (helper arguments included); crossing it throws a TypeError that
 * catching does not undo: the run still fails.
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
  /** Current team ID, or (local Hive) relative to you: 'mine', 'enemies', 'allies';
   * omitted means all permitted teams. */
  team?: number | 'mine' | 'enemies' | 'allies' | null;
  /** Local Hive: a type number, or a name: 'worker' | 'explorer' | 'warrior' for units,
   * a building family name ('inn', 'swarm', 'warflag') for buildings. */
  type?: number | string | null;
  /** Visible results to skip, 0..32768; default 0. */
  offset?: number | null;
  /** Maximum results, 0..32768; default 32768. Large pages may exceed budgets. */
  limit?: number | null;
  /** Local Hive, buildings only: other teams' buildings as the colony last saw them
   * (RememberedBuilding records); type then filters by family name. Any other filter key
   * is refused. */
  remembered?: boolean | null;
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
  /** Local Hive: 'worker' | 'explorer' | 'warrior'. */
  typeName?: string;
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
  /** Local Hive, own buildings and flags: units that have taken the job (workers is the request). */
  assigned?: number;
  /** Variant ID matched to BuildingType.id; shortType is the family code. */
  type: number; shortType: number;
  /** Local Hive: the family name ('inn') and the variant key ('inn.0.site'). */
  familyName?: string; typeName?: string;
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
/** Local Hive: another team's building as the colony last saw it (fog of war). */
export interface RememberedBuilding {
  team: number; familyName: string; x: number; y: number; width: number; height: number;
  last_seen_tick: number; remembered: true; enemy: boolean;
}
export interface TileResource {
  /** Map-local resource ID: look it up in resourceTypes() (maps may add rice, fish,
   * maize...). No resource is 65535 (with variety/amount zero). Presence is
   * type !== 65535, since an eternal resource can have amount zero. */
  type: number;
  /** Local Hive: the resource's key from resourceTypes() ("wheat", "trees", "rocks"). */
  key?: string;
  variety: number; amount: number;
}
export interface Tile {
  x: number; y: number; visible: boolean; explored: boolean;
  // Unexplored tiles have only the four fields above.
  observedTick?: number;
  /** Terrain IDs of the four corners (top-left, top-right, bottom-left,
   * bottom-right); index terrainTypes(). IDs differ between maps. A tile whose
   * corners differ is never buildable. */
  corners?: [number, number, number, number];
  resource?: TileResource;
  /** Visible tiles only: bare occupant IDs, or 65535 for empty/hidden.
   * Find the matching entity list record to obtain its generation. */
  groundUnit?: number; airUnit?: number; building?: number;
  /** AI-only, explored tiles: current own-team forbidden flag. */
  forbidden?: boolean;
}
export interface TerrainType {
  id: number; name: string; editorSelectable: boolean;
  /** Terrain capabilities; occupancy, resources and space still decide placement. */
  buildable: boolean; walkable: boolean; swimmable: boolean; flyable: boolean;
  [property: string]: unknown;
}
export interface ResourceType {
  id: number; key: string; name: string;
  [property: string]: unknown;
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
  /** Shortest offset from a to b on the wrapping map ({x, y} records or ax, ay, bx, by).
   * Local Hive reads only x and y of record arguments, so whole records are cheap. */
  delta(a: {x: number; y: number}, b: {x: number; y: number}): {dx: number; dy: number};
  /** Straight-line distance along that shortest offset. */
  distance(a: {x: number; y: number}, b: {x: number; y: number}): number;
  /** Local Hive: compact row-major arrays (index dy * width + dx) of only the named fields;
   * far cheaper than region(). Element i is tile ((x + i % width) % map.width,
   * (y + floor(i / width)) % map.height). Default fields: ['resource', 'buildable']. */
  scan?(x: number, y: number, width: number, height: number,
    fields?: Array<'explored' | 'visible' | 'resource' | 'amount' | 'terrain' | 'buildable' | 'building'>):
    {x: number; y: number; width: number; height: number; [field: string]: number[] | number};
  /** Local Hive: whether the colony can see room for that building or flag at top-left x, y;
   * reason names the first tile in the way and why (unexplored, a building, a pending
   * placement, a resource, mixed or unbuildable terrain). */
  canPlace?(name: string, x: number, y: number): {ok: boolean; reason?: string};
  /** Local Hive: the nearest such top-left within radius (default 12, at most 64), or null.
   * An unknown name throws, listing the names. */
  findPlacement?(name: string, near: {x: number; y: number; radius?: number}): {x: number; y: number} | null;
}
export interface ColonyEvent {
  kind: 'unit_attacked' | 'building_attacked' | 'building_completed' | 'unit_converted_away' |
    'unit_converted_to_you' | 'building_destroyed' | 'building_demolished' |
    'enemy_building_sighted' | 'enemy_building_gone' | 'hive_program_failed' | 'hive_wake';
  /** Tile: the attacked unit's (not the attacker's), a building's top-left, or
   * building_completed's centre. */
  event: string; x?: number; y?: number; count: number; tick: number;
  /** The other team (conversions, sightings). */
  team?: number;
  /** unit_attacked: the unit type attacked. */
  unit?: 'worker' | 'explorer' | 'warrior';
  /** Building events: its family name. */
  building?: string;
  /** hive_program_failed and hive_wake: the program, and whether it is paused now. */
  program?: string; paused?: boolean;
}
export interface GameRead {
  teams(): Team[];
  /** Stable ascending team/slot order; offsets count visible results only. */
  units(filter?: EntityFilter): Unit[];
  /** Local Hive: with remembered: true, other teams' buildings as last seen. */
  buildings(filter: EntityFilter & {remembered: true}): RememberedBuilding[];
  buildings(filter?: EntityFilter): Building[];
  /** Local Hive: the type a player places for a name (level 0's site, or the flag), or null
   * when the name has no such level or site. An unknown name throws, listing the names. */
  buildingType?(name: string, options?: {level?: number; site?: boolean}): BuildingType | null;
  /** Local Hive: your colony's events after sinceTick (default 0), oldest first. */
  events?(sinceTick?: number): ColonyEvent[];
  /** Hidden, dead, missing or stale => null; malformed IDs throw. */
  unit(reference: EntityRef): Unit | null;
  building(reference: EntityRef): Building | null;
  buildingTypes(): BuildingType[];
  terrainTypes(): TerrainType[];
  resourceTypes(): ResourceType[];
  map: MapRead;
}
export type Order =
  // Basic level-zero site or virtual flag. range is required for virtual flags.
  {type: 'create'; buildingType: number; x: number; y: number;
    workers: number; futureWorkers: number; range?: number} |
  // Local Hive: by palette name, with the click's default units and flag range unless given.
  // x/y must be on the map; an order outside it is rejected, never wrapped.
  {type: 'create'; building: string; x: number; y: number;
    workers?: number; futureWorkers?: number; range?: number} |
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
