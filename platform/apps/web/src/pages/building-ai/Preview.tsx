import { useState } from 'react';
import type { BuildingPackage } from '@glob2/protocol';
/** Display the actual packaged frame dimensions alongside the declared footprint. */
export function BuildingPreview({
  pack,
  assetRoot,
  comparison,
  comparisonLabel = 'Current saved draft',
}: {
  pack: BuildingPackage;
  assetRoot: string;
  comparison?: BuildingPackage;
  comparisonLabel?: string;
}) {
  const [terrain, setTerrain] = useState('grass'),
    [scale, setScale] = useState(1);
  return (
    <div className="building-ai-preview">
      <div className="building-ai-controls">
        <label>
          Ground
          <select value={terrain} onChange={(e) => setTerrain(e.target.value)}>
            <option value="grass">Grass</option>
            <option value="sand">Sand</option>
            <option value="water">Water</option>
          </select>
        </label>
        <label>
          Scale
          <select value={scale} onChange={(e) => setScale(Number(e.target.value))}>
            <option value={1}>Game scale</option>
            <option value={3}>Enlarged 3×</option>
          </select>
        </label>
      </div>
      {pack.variants.map((v) => {
        const sprite = pack.sprites.find((s) => 'package:' + s.key === v.properties['gameSprite']);
        const frame = sprite?.frames[Number(v.properties['gameSpriteImage'] ?? 0)];
        const old = comparison?.variants.find((c) => c.key === v.key);
        const display = (key: string, value: unknown, previous?: unknown) => (
          <div key={key}>
            <dt>{label(key)}</dt>
            <dd>
              {format(value)}
              {comparison && JSON.stringify(value) !== JSON.stringify(previous) && (
                <small className="building-ai-diff">
                  {comparisonLabel}: {previous === undefined ? 'not set' : format(previous)}
                </small>
              )}
            </dd>
          </div>
        );
        return (
          <article key={v.key} className="building-ai-variant">
            <h3>
              {String(
                v.presentation?.['displayName'] ?? v.key.replace('b-' + pack.namespace + '-', ''),
              )}
              {v.properties['isBuildingSite'] ? ' · construction' : ''}
            </h3>
            <div
              className={'building-ai-ground ' + terrain}
              style={{ minHeight: Math.max(150, (frame?.height ?? 64) * scale + 32) }}
            >
              <div
                className="building-ai-footprint"
                style={{
                  width: Number(v.properties['width'] ?? 2) * 32 * scale,
                  height: Number(v.properties['height'] ?? 2) * 32 * scale,
                }}
              />
              {frame ? (
                <span
                  className="building-ai-sprite"
                  style={{ width: frame.width * scale, height: frame.height * scale }}
                >
                  <img
                    src={assetRoot + '/' + frame.imageHash}
                    alt={String(v.presentation?.['displayName'] ?? 'Building frame')}
                    width={frame.width * scale}
                    height={frame.height * scale}
                  />
                  {frame.teamColorHash && (
                    <img
                      className="building-ai-team"
                      src={assetRoot + '/' + frame.teamColorHash}
                      alt=""
                      width={frame.width * scale}
                      height={frame.height * scale}
                    />
                  )}
                </span>
              ) : (
                <p>
                  This building uses installed game artwork. Create or revise its appearance to
                  preview custom artwork here.
                </p>
              )}
            </div>
            <dl>
              {[
                'width',
                'height',
                'hpInit',
                'hpMax',
                'maxUnitInside',
                'armor',
                'viewingRange',
                'shootingRange',
                'shootRhythm',
                'maxBullets',
              ]
                .filter((k) => v.properties[k] !== undefined)
                .map((k) => display(k, v.properties[k], old?.properties[k]))}
              {[
                'constructionCost',
                'repairCost',
                'assignmentLimit',
                'requiredWorkerLevel',
                'regenerationPerTick',
                'projectileDamage',
                'projectileBuildingDamage',
              ]
                .filter((k) => v.semantics[k] !== undefined)
                .map((k) => display(k, v.semantics[k], old?.semantics[k]))}
            </dl>
            <ul>
              {capabilities(v.semantics).map(([name, description]) => (
                <li key={name}>
                  <strong>{name}</strong>: {description}
                </li>
              ))}
            </ul>
            {v.next && <p>Next stage: {v.next.replace('b-' + pack.namespace + '-', '')}</p>}
            <details>
              <summary>All properties and capabilities</summary>
              <pre>
                {JSON.stringify(
                  {
                    properties: v.properties,
                    semantics: v.semantics,
                    presentation: v.presentation,
                  },
                  null,
                  2,
                )}
              </pre>
              {old && (
                <pre aria-label={comparisonLabel + ' properties'}>
                  {JSON.stringify(
                    {
                      properties: old.properties,
                      semantics: old.semantics,
                      presentation: old.presentation,
                    },
                    null,
                    2,
                  )}
                </pre>
              )}
            </details>
          </article>
        );
      })}
    </div>
  );
}
function format(value: unknown): string {
  if (typeof value === 'boolean') return value ? 'Yes' : 'No';
  if (Array.isArray(value)) return value.join(', ');
  if (value && typeof value === 'object')
    return (
      Object.entries(value)
        .filter(([k]) => k !== 'enabled')
        .map(
          ([k, v]) =>
            `${label(k)}: ${k === 'unitMask' && typeof v === 'number' ? classes(v) : format(v)}`,
        )
        .join(' · ') || 'None'
    );
  return String(value);
}
function label(key: string) {
  const names: Record<string, string> = {
    width: 'Footprint width (tiles)',
    height: 'Footprint height (tiles)',
    hpInit: 'Initial health',
    hpMax: 'Maximum health',
    maxUnitInside: 'Interior seats',
    maxUnitWorking: 'Worker slots',
    unitMask: 'Applies to',
    duration: 'Duration (steps)',
    shootRhythm: 'Time between shots (ticks)',
    regenerationPerTick: 'Health restored per tick',
  };
  return (
    names[key] ?? key.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, (c) => c.toUpperCase())
  );
}

function classes(mask: number) {
  return (
    ['workers', 'explorers', 'warriors'].filter((_, i) => mask & (1 << i)).join(', ') || 'none'
  );
}
function capabilities(semantics: Record<string, unknown>): [string, string][] {
  const result: [string, string][] = [];
  for (const key of ['feeding', 'healing']) {
    const service = semantics[key] as { enabled?: boolean } | undefined;
    if (service?.enabled) result.push([label(key), format(service)]);
  }
  const training = semantics['training'];
  if (training && typeof training === 'object')
    for (const [ability, course] of Object.entries(training))
      if (course?.enabled) result.push(['Training: ' + label(ability), format(course)]);
  const production = semantics['production'] as
    { recipes?: Record<string, { enabled?: boolean }> } | undefined;
  for (const [unit, recipe] of Object.entries(production?.recipes ?? {}))
    if (recipe.enabled) result.push(['Produces ' + label(unit), format(recipe)]);
  const market = semantics['market'];
  if (market && typeof market === 'object') {
    const modes = Object.entries(market)
      .filter(([, value]) => value === true)
      .map(([name]) => label(name));
    if (modes.length) result.push(['Supply', modes.join(' · ')]);
  }
  return result;
}
