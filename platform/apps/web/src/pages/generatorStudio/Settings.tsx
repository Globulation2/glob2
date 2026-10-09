import { decodeGeneratorDraft, type GeneratorSettings } from '@glob2/protocol';
export const defaultSettings: GeneratorSettings = {
  seed: 19,
  params: { width: 7, height: 7, teams: 4, workers: 4 },
  candidates: 1,
  startingUnitLevel: 0,
};
interface Control {
  id: string;
  label: string;
  kind?: string;
  minimum?: number;
  maximum?: number;
  step?: number;
  default?: number;
  choices?: string[];
  powerOfTwo?: boolean;
  values?: number[];
}
export function manifestControls(source: string): Control[] {
  try {
    const m = JSON.parse(decodeGeneratorDraft(source).manifest) as { controls?: unknown };
    if (!Array.isArray(m.controls)) return [];
    return m.controls
      .filter(
        (c: unknown): c is Control =>
          !!c &&
          typeof c === 'object' &&
          typeof (c as Control).id === 'string' &&
          typeof (c as Control).label === 'string',
      )
      .slice(0, 64);
  } catch {
    return [];
  }
}
export function effectiveSettings(source: string, settings: GeneratorSettings): GeneratorSettings {
  const defaults = Object.fromEntries(
    manifestControls(source).map((c) => [c.id, typeof c.default === 'number' ? c.default : 0]),
  );
  const shared = ['width', 'height', 'teams', 'workers'];
  const allowed = new Set([...shared, ...Object.keys(defaults)]);
  return {
    ...settings,
    params: {
      ...defaults,
      ...Object.fromEntries(Object.entries(settings.params).filter(([id]) => allowed.has(id))),
    },
  };
}
export default function Settings({
  source,
  value,
  onChange,
}: {
  source: string;
  value: GeneratorSettings;
  onChange: (v: GeneratorSettings) => void;
}) {
  const params = effectiveSettings(source, value).params;
  const set = (id: string, n: number) =>
    onChange({ ...value, params: { ...value.params, [id]: n } });
  return (
    <div className="as-toolbar">
      <label>
        Seed{' '}
        <input
          type="number"
          min={0}
          max={4294967295}
          value={value.seed}
          onChange={(e) => onChange({ ...value, seed: Number(e.target.value) })}
        />
      </label>
      <button
        onClick={() =>
          onChange({ ...value, seed: crypto.getRandomValues(new Uint32Array(1))[0] ?? 0 })
        }
      >
        Reroll seed
      </button>
      {['width', 'height'].map((id) => (
        <label key={id}>
          {id === 'width' ? 'Width' : 'Height'}{' '}
          <select value={params[id]} onChange={(e) => set(id, Number(e.target.value))}>
            {[6, 7, 8, 9].map((n) => (
              <option key={n} value={n}>
                {2 ** n} tiles
              </option>
            ))}
          </select>
        </label>
      ))}
      <label>
        Colonies{' '}
        <input
          type="number"
          min={1}
          max={16}
          value={params.teams}
          onChange={(e) => set('teams', Number(e.target.value))}
        />
      </label>
      <label>
        Starting workers{' '}
        <input
          type="number"
          min={1}
          max={8}
          value={params.workers}
          onChange={(e) => set('workers', Number(e.target.value))}
        />
      </label>
      {manifestControls(source).map((c) => (
        <label key={c.id}>
          {c.label}
          {c.kind === 'toggle' ? (
            <input
              type="checkbox"
              checked={params[c.id] === 1}
              onChange={(e) => set(c.id, e.target.checked ? 1 : 0)}
            />
          ) : c.kind === 'choice' &&
            Array.isArray(c.choices) &&
            c.choices.every((label) => typeof label === 'string') ? (
            <select value={params[c.id]} onChange={(e) => set(c.id, Number(e.target.value))}>
              {c.choices.slice(0, 256).map((label, i) => (
                <option key={i} value={i}>
                  {label}
                </option>
              ))}
            </select>
          ) : Array.isArray(c.values) && c.values.length && c.values.every(Number.isSafeInteger) ? (
            <select value={params[c.id]} onChange={(e) => set(c.id, Number(e.target.value))}>
              {c.values.slice(0, 4096).map((n) => (
                <option key={n} value={n}>
                  {c.powerOfTwo ? 2 ** n : n}
                </option>
              ))}
            </select>
          ) : (
            <>
              <input
                type="number"
                min={c.minimum}
                max={c.maximum}
                step={c.step ?? 1}
                value={params[c.id]}
                onChange={(e) => set(c.id, Number(e.target.value))}
              />
              {c.powerOfTwo && <span> ({2 ** (params[c.id] ?? 0)})</span>}
            </>
          )}
        </label>
      ))}
      <p>Changing settings takes effect when you press Generate. One seed per preview.</p>
    </div>
  );
}
