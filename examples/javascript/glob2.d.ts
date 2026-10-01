/** Glob2 synchronous scripting profile 1. Read results are detached copies. */
export type Data = null | boolean | number | string | Data[] | {[key: string]: Data};
export interface EntityRef { id: number; generation: number }
export interface Unit extends EntityRef {
  team: number; type: number; x: number; y: number; hp: number; maxHp: number;
  levels: number[]; performance: number[];
  experience?: number; experienceLevel?: number; hunger?: number; fruitCount?: number;
  activity?: number; movement?: number; action?: number; medical?: number;
  carriedResource?: number; speed?: number; direction?: number;
  insideTimeout?: number; fruitMask?: number; destinationPurpose?: number;
  targetX?: number; targetY?: number;
  attachedBuilding?: EntityRef | null; targetBuilding?: EntityRef | null;
}
export interface Building extends EntityRef {
  team: number; type: number; shortType: number; x: number; y: number;
  hp: number; maxHp: number; level: number; virtual: boolean; construction: number;
  workers?: number; futureWorkers?: number; priority?: number; range?: number;
  minimumLevel?: number; resources?: number[]; wishedResources?: number[];
  production?: number[]; productionTimeout?: number; receiveMask?: number;
  sendMask?: number; bullets?: number; clearingResources?: boolean[];
}
export interface Tile {
  x: number; y: number; visible: boolean; explored: boolean; observedTick?: number;
  terrain?: number; resource?: {type: number; variety: number; amount: number};
  fertility?: number; groundUnit?: number; airUnit?: number; building?: number;
  forbidden?: boolean;
}
export interface BuildingType {
  id: number; name: string; shortType: number; level: number; site: boolean;
  virtual: boolean; width: number; height: number; maxHp: number;
  maxWorkers: number; resourceCapacity: number[];
}
export interface Context {
  tick: number; myTeam: number; random(): number;
  game: {
    teams(): {id: number; alive: boolean; allies?: number; resources?: number[]}[];
    units(filter?: {team?: number; offset?: number; limit?: number}): Unit[];
    buildings(filter?: {team?: number; offset?: number; limit?: number}): Building[];
    unit(reference: EntityRef): Unit | null;
    building(reference: EntityRef): Building | null;
    buildingTypes(): BuildingType[];
    map: {width: number; height: number; tile(x: number,y: number): Tile;
      region(x: number,y: number,width: number,height: number): Tile[]};
    /** Scenario capability only. */
    objectives(): {id: number; scriptNumber: number; text: string; type: number;
      visible: boolean; complete: boolean; failed: boolean}[];
    hints(): {id: number; scriptNumber: number; text: string; visible: boolean}[];
    interface(): {[key: string]: Data};
  };
}
export type Order = null |
  {type: 'create'; buildingType: number; x: number; y: number; workers: number; futureWorkers: number; range?: number} |
  {type: 'workers' | 'cancelConstruction'; building: EntityRef; workers: number} |
  {type: 'construction'; building: EntityRef; workers: number; futureWorkers: number} |
  {type: 'delete' | 'cancelDelete'; building: EntityRef} |
  {type: 'priority'; building: EntityRef; priority: -1 | 0 | 1} |
  {type: 'production'; building: EntityRef; ratios: [number,number,number]} |
  {type: 'exchange'; building: EntityRef; receiveMask: number; sendMask: number} |
  {type: 'range'; building: EntityRef; range: number} |
  {type: 'minimumLevel'; building: EntityRef; level: number} |
  {type: 'moveFlag'; building: EntityRef; x: number; y: number} |
  {type: 'clearingResources'; building: EntityRef; resources: [boolean,boolean,boolean,false,boolean]} |
  {type: 'forbidden' | 'guardArea' | 'clearArea'; x: number; y: number; width: number; height: number; mode: 1 | 2; mask: boolean[]};
export type Effect =
  {type: 'message'; text: string} |
  {type: 'messageTranslated'; text: string; language: string} |
  {type: 'hideMessage'} |
  {type: 'objective'; id: number; action: 'complete' | 'incomplete' | 'failed' | 'hidden' | 'visible'} |
  {type: 'hint'; id: number; visible: boolean} |
  {type: 'buildingChoice' | 'flagChoice'; name: string; enabled: boolean} |
  {type: 'guiElement'; id: number; enabled: boolean};
