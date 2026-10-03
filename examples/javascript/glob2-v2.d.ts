/** Authoritative profile 2 declarations. Engine save format 129; API profile 2.
 * Copy this file AND glob2.d.ts to authoring projects. Never import at runtime.
 * All spatial operations are synchronous and bounded. */
import type {
  Context,
  GameRead,
  Building,
  EntityRef,
  EntityFilter,
  MapRead,
  Tile,
  Order,
  Priority,
  BuildingName,
  FlagName,
} from "./glob2";
export interface Metadata {
  apiVersion: 2;
  name: string;
  description?: string;
  version?: string;
  author?: string;
}
export interface Point {
  x: number;
  y: number;
}
export interface Region extends Point {
  width: number;
  height: number;
}
export interface ManagedBuilding extends Readonly<Building> {
  readonly ref: EntityRef;
  /** Last observation, before pending edits. Reacquire handles every callback. */
  readonly observed: Readonly<Building>;
  workers: number;
  priority: Priority;
  production: [number, number, number];
  receiveMask: number;
  sendMask: number;
  /** Position/range/minimumLevel are writable only on flags. */
  x: number;
  y: number;
  range: number;
  minimumLevel: number;
  clearingResources: [boolean, boolean, boolean, boolean, boolean];
}
export interface Sources {
  resource?:
    | "wood"
    | "wheat"
    | "papyrus"
    | "stone"
    | "alga"
    | "cherry"
    | "orange"
    | "prune"
    | number;
  harvestable?: boolean;
  points?: Point[];
  units?: {
    team?: number;
    relation?: "any" | "own" | "ally" | "enemy";
    type?: "worker" | "explorer" | "warrior" | number;
  };
  buildings?: {
    team?: number;
    virtual?: boolean;
    relation?: "any" | "own" | "ally" | "enemy";
    type?: number | BuildingName | FlagName;
  };
  weight?: "count" | "amount" | "strength";
  /** Integer multipliers 0..1000 for hp × attack strength; default 1 each. */
  strength?: { worker?: number; explorer?: number; warrior?: number };
}
export interface FieldSpec {
  sources: Sources;
  movement?: "walk" | "swim" | "fly";
  metric?: "path" | "manhattan" | "chebyshev";
}
/** Opaque callback-local handle: never persist this in module state. */
export interface Field {
  readonly id: number;
  readonly tick: number;
}
export interface Metric {
  metric: "distance" | "fertility" | "resourceDensity" | "threat";
  sources?: Sources;
  resource?: Sources["resource"];
  harvestable?: boolean;
  radius?: number;
  movement?: FieldSpec["movement"];
  distanceMetric?: FieldSpec["metric"];
  /** Positive scores preferred; negative weights prefer smaller values. Integers. */
  weight?: number;
  min?: number;
  max?: number;
}
export interface Placement {
  building: BuildingName | FlagName;
  workers?: number;
  futureWorkers?: number;
  range?: number;
  region?: Region;
  anchor?: Point;
  movement?: FieldSpec["movement"];
  reachable?: boolean;
  clearance?: number;
  reserveUpgrade?: boolean;
  limit?: number;
  constraints?: Metric[];
  preferences?: Metric[];
}
export interface PlacementResult {
  found: boolean;
  reason?: string;
  rejectedSpace: number;
  rejectedConstraints: number;
  candidates: {
    x: number;
    y: number;
    score: number;
    scores: { metric: string; value: number; weight: number }[];
  }[];
  order?: Extract<Order, { type: "create" }>;
}
export interface Spatial {
  distance(a: Point, b: Point, metric?: "chebyshev" | "manhattan"): number;
  displacement(a: Point, b: Point): Point;
  footprintDistance(a: Region, b: Region): number;
  overlap(a: Region, b: Region): boolean;
  distanceField(spec: FieldSpec): Field;
  fieldValue(
    field: Field,
    x: number,
    y: number,
  ): {
    known: boolean;
    reachable: boolean | null;
    distance: number | null;
    observedTick: number;
  };
  passable(point: Point & { movement?: FieldSpec["movement"] }): boolean | null;
  summary(
    region: Region & { resource?: Sources["resource"]; harvestable?: boolean },
  ): {
    knownTiles: number;
    visibleTiles: number;
    resourceTiles: number;
    amount: number;
    fertility: number | null;
  };
  hotspots(spec: {
    sources: Sources;
    radius?: number;
    limit?: number;
  }): { x: number; y: number; score: number }[];
  components(spec: {
    movement?: FieldSpec["movement"];
  }): { id: number; x: number; y: number; tiles: number }[];
  components(spec: Point & { movement?: FieldSpec["movement"] }): number | null;
  placement(spec: Placement): PlacementResult;
}
export interface ActionStatus {
  id: number;
  status:
    | "pending"
    | "issued"
    | "constructing"
    | "completed"
    | "failed"
    | "cancelled";
  reason?: string;
  building?: EntityRef;
  tick: number;
}
export interface Actions {
  create(spec: Omit<Extract<Order, { type: "create" }>, "type">): number;
  build(spec: Placement): number | null;
  upgrade(spec: {
    building: EntityRef;
    workers: number;
    futureWorkers: number;
  }): number;
  repair(spec: {
    building: EntityRef;
    workers: number;
    futureWorkers: number;
  }): number;
  delete(spec: { building: EntityRef }): number;
  zone(
    spec: Extract<Order, { type: "forbidden" | "guardArea" | "clearArea" | "farmArea" }>,
  ): number;
  status(id: number): ActionStatus | null;
  cancel(id: number): number;
}
export interface ContextV2 extends Omit<Context, "game"> {
  game: Omit<GameRead, "building" | "buildings" | "map"> & {
    map: Omit<MapRead, "tile" | "region"> & {
      tile(x: number, y: number): Readonly<Tile> & { fertility?: number };
      region(
        x: number,
        y: number,
        width: number,
        height: number,
      ): (Readonly<Tile> & { fertility?: number })[];
    };
    building(ref: EntityRef): ManagedBuilding | null;
    buildings(filter?: EntityFilter): ManagedBuilding[];
  };
  spatial: Spatial;
  actions: Actions;
  telemetry: {
    set(
      name: string,
      value: number | boolean | string,
      description?: string | { unit?: string; description?: string },
    ): void;
  };
}
export type Step = (ctx: ContextV2) => void;
