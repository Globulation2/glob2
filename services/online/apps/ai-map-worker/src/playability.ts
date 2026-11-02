import type { StudioSettings, StudioCheck } from '@glob2/protocol';
// Versioned minimum contract, not a competitive-balance claim. The report is produced
// after native legalization/settlement; never validate only the provider's pixels.
export const PLAYABILITY_VERSION = 'ai-playable-v1';
const finite = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);
const object = (value: unknown): Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : {};
const array = (value: unknown): unknown[] => (Array.isArray(value) ? value : []);

function checklist(settings: StudioSettings): StudioCheck[] {
  const checks: StudioCheck[] = [
    { id: 'request', label: 'Requested size and players', status: 'pending' },
    { id: 'starts', label: 'Valid colony starts', status: 'pending' },
    { id: 'growth', label: 'Resource growth enabled', status: 'pending' },
    { id: 'routes', label: 'Connected walking routes', status: 'pending' },
    { id: 'economy', label: 'Opening economy measured', status: 'pending' },
  ];
  for (let i = 0; i < settings.players; i++)
    for (const [id, label] of [
      ['building', 'building space'],
      ['wheat', 'starter food'],
      ['wood', 'starter timber'],
      ['renewable', 'renewable food ground'],
    ])
      checks.push({
        id: `${id}-${i}`,
        label: `Colony ${i + 1}: ${label}`,
        status: 'pending',
        colony: i,
      });
  return checks;
}

// A generator lets the worker durably publish each outcome as it is evaluated.
// Invalid prerequisites suppress only dependent checks; independent failures remain visible.
export function* playabilityChecks(raw: unknown, settings: StudioSettings): Generator<StudioCheck> {
  yield* checklist(settings);
  const report = object(raw),
    map = object(report['map']);
  const colonies = array(map['colonies']);
  const walking = object(object(report['movement'])['walking']);
  const walkingColonies = array(walking['colonies']);
  const quality = object(report['canonical_quality']);
  const qualityColonies = array(quality['colonies']);
  function* check(
    id: string,
    label: string,
    evaluate: () => boolean,
    failure: string,
    prerequisite = true,
    colony?: number,
  ): Generator<StudioCheck> {
    const start = object(object(colonies[colony === undefined ? -1 : colony])['start']);
    const x = start['x'],
      y = start['y'];
    const context = {
      ...(colony === undefined ? {} : { colony }),
      ...(finite(x) && finite(y) && x >= 0 && x < settings.width && y >= 0 && y < settings.height
        ? { location: { x, y } }
        : {}),
    };
    if (!prerequisite) {
      yield {
        id,
        label,
        status: 'not-evaluated',
        detail: 'Required native measurements are unavailable.',
        ...context,
      };
      return;
    }
    yield { id, label, status: 'running', ...context };
    const passed = evaluate();
    yield {
      id,
      label,
      status: passed ? 'passed' : 'failed',
      ...(passed ? {} : { detail: failure }),
      ...context,
    };
  }
  const shape =
    report['schema_version'] === 2 &&
    map['width'] === settings.width &&
    map['height'] === settings.height &&
    map['player_slots'] === settings.players;
  yield* check(
    'request',
    'Requested size and players',
    () => shape,
    'Map report does not match the request.',
  );
  yield* check(
    'starts',
    'Valid colony starts',
    () =>
      colonies.length === settings.players &&
      colonies.every((rawColony) => {
        const colony = object(rawColony),
          start = object(colony['start']);
        const x = start['x'],
          y = start['y'];
        return (
          colony['alive'] === true &&
          finite(x) &&
          finite(y) &&
          x >= 0 &&
          x < settings.width &&
          y >= 0 &&
          y < settings.height
        );
      }),
    'Every colony needs a valid start.',
    shape,
  );
  yield* check(
    'growth',
    'Resource growth enabled',
    () => object(object(report['space'])['growth_disabled'])['tiles'] === 0,
    'Generated maps cannot disable resource growth.',
    shape,
  );
  yield* check(
    'routes',
    'Connected walking routes',
    () =>
      walking['unreachable_directed_pairs'] === 0 && walkingColonies.length === settings.players,
    'Colony homes must have connected walking routes.',
    shape,
  );
  const measured = quality['measured'] === true && qualityColonies.length === settings.players;
  yield* check(
    'economy',
    'Opening economy measured',
    () => measured,
    'Opening economy could not be assessed.',
    shape,
  );
  for (let i = 0; i < settings.players; i++) {
    const c = object(walkingColonies[i]),
      q = object(object(qualityColonies[i])['raw']);
    const available = shape && measured && walkingColonies.length === settings.players;
    const sites = c['catchment_build_sites_4x4'],
      buildable = q['catchment_buildable_tiles'];
    yield* check(
      `building-${i}`,
      `Colony ${i + 1}: building space`,
      () => finite(sites) && sites >= 16 && finite(buildable) && buildable >= 128,
      'A colony has insufficient nearby building space.',
      available,
      i,
    );
    for (const [name, label, minimum] of [
      ['wheat', 'starter food', 64],
      ['wood', 'starter timber', 16],
    ] as const) {
      const r = object(object(c['resources'])[name]);
      const cost = r['nearest_gather_cost'],
        stored = r['catchment_stored_amount'];
      yield* check(
        `${name}-${i}`,
        `Colony ${i + 1}: ${label}`,
        () => finite(cost) && cost <= 12 && finite(stored) && stored >= minimum,
        'A colony lacks accessible starter food or timber.',
        shape && walkingColonies.length === settings.players,
        i,
      );
    }
    const grass = q['catchment_fertile_grass_tiles'],
      fertility = q['mean_fertility'];
    yield* check(
      `renewable-${i}`,
      `Colony ${i + 1}: renewable food ground`,
      () => finite(grass) && grass >= 64 && finite(fertility) && fertility > 0,
      'A colony lacks renewable food ground.',
      shape && measured,
      i,
    );
  }
}

export function validatePlayability(raw: unknown, settings: StudioSettings) {
  const checks = [...playabilityChecks(raw, settings)];
  const failure = checks.find((check) => check.status === 'failed');
  if (failure) throw new Error(failure.detail);
  if (checks.some((check) => check.status === 'not-evaluated'))
    throw new Error('Opening economy could not be assessed.');
  return { contract: PLAYABILITY_VERSION, passed: true };
}
