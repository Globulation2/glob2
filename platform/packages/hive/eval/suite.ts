// Held out from the system prompt and executable teaching examples.
// Objectives deliberately distinguish accepted commands from observed game state.
export interface Scenario {
  id: string;
  category: string;
  command: string;
  expected: Record<string, unknown>;
}
export const suite: Scenario[] = [
  {
    id: 'count-workers',
    category: 'investigation',
    command: 'Count our workers. Return a structured output with workers as the count.',
    expected: { output: { workers: 12 } },
  },
  {
    id: 'count-inns',
    category: 'investigation',
    command: 'How many inns do we own? Return structured output with inns as the count.',
    expected: { output: { inns: 1 } },
  },
  {
    id: 'own-only',
    category: 'investigation',
    command:
      'List only our buildings in structured output with buildings as an array of {id, generation, team}.',
    expected: { ownedList: 2 },
  },
  {
    id: 'workforce-production',
    category: 'production',
    command:
      'Set all our swarms to produce workers only, with weights [8,0,0]. Confirm the settings.',
    expected: { family: 0, field: 'production', value: [8, 0, 0] },
  },
  {
    id: 'balanced-production',
    category: 'production',
    command:
      'Set our swarm production weights to 3 workers, 2 explorers, 1 warrior. Confirm it took effect.',
    expected: { family: 0, field: 'production', value: [3, 2, 1] },
  },
  {
    id: 'inn-staffing',
    category: 'production',
    command: 'Assign a limit of 5 workers to our existing inn. Verify the requested limit.',
    expected: { family: 1, field: 'workers', value: 5 },
  },
  {
    id: 'swarm-staffing',
    category: 'production',
    command: 'Set our swarm worker limit to 7. Confirm the requested setting.',
    expected: { family: 0, field: 'workers', value: 7 },
  },
  {
    id: 'inn-priority',
    category: 'production',
    command: 'Make our existing inn high priority, then check it.',
    expected: { family: 1, field: 'priority', value: 1 },
  },
  {
    id: 'swarm-priority',
    category: 'production',
    command: 'Set our existing swarm to low priority and verify.',
    expected: { family: 0, field: 'priority', value: -1 },
  },
  {
    id: 'start-inn',
    category: 'construction',
    command:
      'Start one new level-zero inn at tile (18,4), with 3 construction workers and 2 future workers. Stop once the construction site exists.',
    expected: { createdFamily: 1, x: 18, y: 4 },
  },
  {
    id: 'start-school',
    category: 'construction',
    command:
      'Start a level-zero school at tile (18,10), with 2 construction and 1 future worker. Verify that the site exists.',
    expected: { createdFamily: 6, x: 18, y: 10 },
  },
  {
    id: 'start-tower',
    category: 'defence',
    command:
      'Start a level-zero defence tower at tile (18,18), with 2 construction and 1 future worker. Confirm the site.',
    expected: { createdFamily: 7, x: 18, y: 18 },
  },
  {
    id: 'explore-east',
    category: 'exploration',
    command:
      'Place an exploration flag at tile (20,8), range 4, with a limit of 2 explorers. Verify it exists.',
    expected: { createdFamily: 8, x: 20, y: 8 },
  },
  {
    id: 'explore-north',
    category: 'exploration',
    command:
      'Place an exploration flag at tile (15,2), range 6 and 1 worker, then confirm it exists.',
    expected: { createdFamily: 8, x: 15, y: 2 },
  },
  {
    id: 'rally-defence',
    category: 'defence',
    command: 'Place a war flag at tile (16,16), radius 5 and limit 3 warriors. Verify it exists.',
    expected: { createdFamily: 9, x: 16, y: 16 },
  },
  {
    id: 'rally-south',
    category: 'defence',
    command: 'Set a war flag at tile (8,20), radius 3, limit 1 warrior. Confirm it exists.',
    expected: { createdFamily: 9, x: 8, y: 20 },
  },
  {
    id: 'standing-workers',
    category: 'recurring',
    command: 'Keep our inn worker limit at 4. Install a standing order checking every 25 ticks.',
    expected: { standing: true, family: 1, field: 'workers', value: 4 },
  },
  {
    id: 'standing-production',
    category: 'recurring',
    command: 'Keep swarm production at weights [5,1,2]. Install a standing order every 50 ticks.',
    expected: { standing: true, family: 0, field: 'production', value: [5, 1, 2] },
  },
  {
    id: 'population-trigger',
    category: 'trigger',
    command:
      'Keep watch over our worker population every 25 ticks. Wake yourself once when we have at least 10 workers, with key population-ready. Install that standing order.',
    expected: { standing: true, wake: 'population-ready' },
  },
  {
    id: 'inn-trigger',
    category: 'trigger',
    command:
      'Watch for at least one owned inn every 25 ticks. Wake yourself once with key inn-ready when that condition is met. Install the standing order.',
    expected: { standing: true, wake: 'inn-ready' },
  },
];
