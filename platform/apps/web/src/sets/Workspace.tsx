import { displayMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { ReleaseDialog } from '../components/studio/Studio.tsx';
import { Icon } from '../icons.tsx';
import { createContext, useContext, useEffect, useState } from 'react';
import type { SetDraft, SetPackage, SetInfo } from '@glob2/protocol';
import { request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
import {
  MATERIALS,
  TERRAIN_PRESETS,
  TERRAIN_PROPERTIES,
  RESOURCE_PRESETS,
  namespace,
  newPackage,
  sheetFromFile,
  sheetUrl,
} from './model.ts';
import './sets.css';
import { SetPreview } from './Preview.tsx';
type Entry = Record<string, unknown>;
const object = (v: unknown): Entry =>
  v && typeof v === 'object' && !Array.isArray(v) ? (v as Entry) : {};
const name = (key: string) =>
  key
    .replace(/([A-Z])/g, ' $1')
    .replaceAll('_', ' ')
    .replace(/^./, (x) => x.toUpperCase());

type JsonEdit = { text: string; error: string; value: string };
const JsonEdits = createContext<{
  edits: Record<string, JsonEdit>;
  edit: (id: string, next: JsonEdit) => void;
} | null>(null);

function ObjectControls({
  value,
  onChange,
  idPrefix,
}: {
  value: Entry;
  onChange: (v: Entry) => void;
  idPrefix: string;
}) {
  useLocale();
  return (
    <div className="set-properties">
      {Object.entries(value).map(([key, v]) => (
        <label key={key}>
          {name(key)}
          {typeof v === 'boolean' ? (
            <input
              type="checkbox"
              checked={v}
              onChange={(e) => onChange({ ...value, [key]: e.target.checked })}
            />
          ) : typeof v === 'number' ? (
            <input
              type="number"
              value={v}
              onChange={(e) => onChange({ ...value, [key]: Number(e.target.value) })}
            />
          ) : typeof v === 'string' ? (
            <input value={v} onChange={(e) => onChange({ ...value, [key]: e.target.value })} />
          ) : v === null ? (
            <select
              value=""
              onChange={(e) => onChange({ ...value, [key]: e.target.value || null })}
            >
              <option value="">{t('None')}</option>
              {MATERIALS.map((m) => (
                <option key={m}>{m}</option>
              ))}
            </select>
          ) : (
            <JsonControl
              id={idPrefix + '/' + key}
              value={v}
              onChange={(next) => onChange({ ...value, [key]: next })}
            />
          )}
          {key.endsWith('Q8') && (
            <small>
              {key.includes('Health')
                ? t('Signed HP per exposed tick, divided by 256; negative values damage units.')
                : t('Fixed-point units: 256 = 1, 128 = 0.5, 512 = 2.')}
            </small>
          )}
          {(key === 'growthRate' || key === 'spreadRate') && (
            <small>
              {t(
                'Integer rate: 196608 means one growth opportunity or probability one. Zero disables it.',
              )}
            </small>
          )}
        </label>
      ))}
    </div>
  );
}
function JsonControl({
  id,
  value,
  onChange,
}: {
  id: string;
  value: unknown;
  onChange: (v: unknown) => void;
}) {
  useLocale();
  const state = useContext(JsonEdits);
  if (!state) throw Error(t('JSON controls require the set workspace.'));
  // Keep unfinished text in the workspace so switching entries cannot discard it.
  const edited = state.edits[id];
  return (
    <>
      <textarea
        aria-label={t('Advanced properties JSON')}
        aria-invalid={!!edited?.error}
        value={
          edited && (edited.error || edited.value === JSON.stringify(value))
            ? edited.text
            : JSON.stringify(value, null, 2)
        }
        onChange={(e) => {
          const text = e.target.value;
          let error = '',
            canonical = JSON.stringify(value);
          try {
            const next = JSON.parse(text);
            onChange(next);
            canonical = JSON.stringify(next);
          } catch (e) {
            error = e instanceof Error ? e.message : 'Enter valid JSON before saving.';
          }
          state.edit(id, { text, error, value: canonical });
        }}
      />
      {edited?.error && <small role="alert">{edited.error}</small>}
    </>
  );
}

function Variants({
  value,
  frame,
  onChange,
}: {
  value: unknown;
  frame: number;
  onChange: (v: unknown) => void;
}) {
  useLocale();
  const entries = Array.isArray(value) ? value.map(object) : [];
  return (
    <div className="set-variants">
      {entries.map((v, i) => (
        <div key={i}>
          <label>
            {t('Frame')}
            <input
              type="number"
              min={0}
              value={Number(v['frame'] ?? 0)}
              onChange={(e) =>
                onChange(
                  entries.map((x, j) => (i === j ? { ...x, frame: Number(e.target.value) } : x)),
                )
              }
            />
          </label>
          <label>
            {t('Weight')}
            <input
              type="number"
              min={1}
              value={Number(v['weight'] ?? 1)}
              onChange={(e) =>
                onChange(
                  entries.map((x, j) => (i === j ? { ...x, weight: Number(e.target.value) } : x)),
                )
              }
            />
          </label>
          <button
            type="button"
            disabled={entries.length === 1}
            onClick={() => onChange(entries.filter((_, j) => i !== j))}
          >
            {t('Remove variant')}
          </button>
        </div>
      ))}
      <button type="button" onClick={() => onChange([...entries, { frame, weight: 1 }])}>
        {t('Add selected frame')}
      </button>
    </div>
  );
}
export function SetEditor({
  id,
  onDirtyChange,
  onSaved,
  serverRevision,
}: {
  id?: string;
  onDirtyChange?: (dirty: boolean) => void;
  onSaved?: () => void;
  serverRevision?: number;
}) {
  useLocale();
  const [releaseOpen, setReleaseOpen] = useState(false);
  const { account } = useSession(),
    { navigate } = useRouter();
  const [pack, setPack] = useState<SetPackage | null>(null),
    [draft, setDraft] = useState<SetDraft | null>(null),
    [dirty, setDirty] = useState(false),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  const [kind, setKind] = useState<'terrain' | 'resource'>('terrain'),
    [selected, setSelected] = useState(0),
    [preset, setPreset] = useState('grass'),
    [resourcePreset, setResourcePreset] = useState('trees'),
    [frame, setFrame] = useState(0),
    [sheet, setSheet] = useState(''),
    [frameWidth, setFrameWidth] = useState(32),
    [frameHeight, setFrameHeight] = useState(32),
    [label, setLabel] = useState('1.0'),
    [notes, setNotes] = useState(''),
    [visibility, setVisibility] = useState('public'),
    [tagsText, setTagsText] = useState(''),
    [jsonEdits, setJsonEdits] = useState<Record<string, JsonEdit>>({});
  useEffect(() => {
    if (!id) {
      if (account) {
        const abort = new AbortController();
        queueMicrotask(() => {
          if (!abort.signal.aborted) setPack(newPackage(account.displayName));
        });
        return () => abort.abort();
      }
      return;
    }
    const abort = new AbortController();
    void request<SetDraft>('GET', '/api/v1/set-drafts/' + id, { signal: abort.signal }).then(
      (d) => {
        setDraft(d);
        setPack(d.package);
        setTagsText(d.package.tags.join(', '));
      },
      (e) => {
        if (!abort.signal.aborted) setError(String(e.message));
      },
    );
    return () => abort.abort();
  }, [id, account]);
  useEffect(() => {
    if (draft?.validation?.status !== 'pending') return;
    const abort = new AbortController();
    const timer = setInterval(() => {
      void request<SetDraft>('GET', '/api/v1/set-drafts/' + draft.id, {
        signal: abort.signal,
      }).then(
        (d) => setDraft(d),
        (e) => {
          if (!abort.signal.aborted) setError(String(e.message));
        },
      );
    }, 2000);
    return () => {
      clearInterval(timer);
      abort.abort();
    };
  }, [draft?.id, draft?.validation?.status]);
  useEffect(() => {
    const warn = (e: BeforeUnloadEvent) => {
      if (dirty) {
        e.preventDefault();
        e.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', warn);
    const navigate = (e: Event) => {
      if (dirty && !window.confirm(t('Leave this set and discard unsaved changes?')))
        e.preventDefault();
    };
    window.addEventListener('glob2-before-navigate', navigate);
    return () => {
      window.removeEventListener('beforeunload', warn);
      window.removeEventListener('glob2-before-navigate', navigate);
    };
  }, [dirty]);
  useEffect(() => {
    onDirtyChange?.(dirty);
  }, [dirty, onDirtyChange]);
  function change(next: SetPackage) {
    setPack(next);
    setDirty(true);
  }
  async function action(fn: () => Promise<void>) {
    setBusy(true);
    setError('');
    try {
      await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }
  async function save() {
    if (!pack) throw Error(t('The workspace is still loading.'));
    if (Object.values(jsonEdits).some((edit) => edit.error))
      throw Error(t('Correct the invalid JSON before saving.'));
    const current = draft
      ? await request<SetDraft>('PUT', '/api/v1/set-drafts/' + draft.id, {
          body: { revision: draft.revision, package: pack },
        })
      : await request<SetDraft>('POST', '/api/v1/set-drafts', { body: pack });
    setDraft(current);
    setDirty(false);
    onSaved?.();
    return current;
  }
  if (!account)
    return (
      <section>
        <h1>{t('Create a set')}</h1>
        <p>{t('Sign in with a registered account to save and publish custom sets.')}</p>
        <a href="/signin">{t('Sign in')}</a>
      </section>
    );
  if (!pack) return <p role={error ? 'alert' : 'status'}>{error || t('Loading workspace…')}</p>;
  const entries = kind === 'terrain' ? pack.terrains : pack.resources,
    entry = entries[selected];
  const material = entry ? object(pack.assets.terrains[String(entry['key'])]) : {},
    presentation = entry ? object(entry['presentation']) : {};
  const activeSheet = pack.assets.sheets.find((s) => s.hash === sheet);
  const checked = !dirty && draft?.validation?.status === 'valid',
    published = !!draft?.publishedVersionId;
  function updateEntry(next: Entry) {
    if (!pack || !entry) return;
    change({
      ...pack,
      [kind === 'terrain' ? 'terrains' : 'resources']: entries.map((e, i) =>
        i === selected ? next : e,
      ),
    });
  }
  function updateMaterial(next: Entry) {
    if (!pack || !entry) return;
    change({
      ...pack,
      assets: {
        ...pack.assets,
        terrains: { ...pack.assets.terrains, [String(entry['key'])]: next },
      },
    });
  }
  function addEntry() {
    if (!pack) return;
    // Suffixes are stable release identities: never reuse a key still present after deletion.
    const prefix = namespace(pack) + (kind === 'terrain' ? preset : resourcePreset) + '-';
    const used = new Set([...pack.terrains, ...pack.resources].map((e) => e['key']));
    let suffix = 1;
    while (used.has(prefix + suffix)) suffix++;
    const key = prefix + suffix;
    const source = RESOURCE_PRESETS.find((r) => r.key === resourcePreset) ?? RESOURCE_PRESETS[0];
    if (!source) return;
    const added: Entry =
      kind === 'terrain'
        ? { key, name: 'Custom ' + name(preset), base: preset, appearance: preset, properties: {} }
        : {
            ...structuredClone(source),
            key,
            requiredExperiment: '',
            presentation: { ...source.presentation, name: 'Custom ' + source.presentation.name },
          };
    change({ ...pack, [kind === 'terrain' ? 'terrains' : 'resources']: [...entries, added] });
    setSelected(entries.length);
  }
  return (
    <JsonEdits.Provider
      value={{
        edits: jsonEdits,
        edit: (id, next) => {
          setJsonEdits((previous) => ({ ...previous, [id]: next }));
          setDirty(true);
        },
      }}
    >
      <section className="set-workspace">
        {draft && dirty && serverRevision !== undefined && serverRevision !== draft.revision && (
          <div role="alert">
            <p>
              <RichMessage
                source={'Another session saved revision {slot0}. Your local edits are preserved.'}
                slots={{ slot0: serverRevision }}
              />
            </p>
            <button
              onClick={() => {
                const url = URL.createObjectURL(
                  new Blob([JSON.stringify({ package: pack, jsonEdits }, null, 2)], {
                    type: 'application/json',
                  }),
                );
                const link = document.createElement('a');
                link.href = url;
                link.download = 'terrain-local-recovery.json';
                link.click();
                setTimeout(() => URL.revokeObjectURL(url), 1000);
              }}
            >
              {t('Download local changes')}
            </button>
            <button
              onClick={() =>
                void action(async () => {
                  const current = await request<SetDraft>('GET', '/api/v1/set-drafts/' + draft.id);
                  setDraft(current);
                  setPack(current.package);
                  setJsonEdits({});
                  setDirty(false);
                  onSaved?.();
                })
              }
            >
              {t('Discard manual edits and load saved revision')}
            </button>
          </div>
        )}
        <header className="set-heading">
          <div>
            {draft && !published && !onDirtyChange && (
              <Link to={`/terrain-studio?draft=${draft.id}`} className="button">
                {t('Edit with AI')}
              </Link>
            )}
            {onDirtyChange ? <h2>{pack.title}</h2> : <h1>{pack.title}</h1>}
            <p>
              <RichMessage
                source={'{slot0} · Custom artwork and gameplay properties'}
                slots={{
                  slot0: published
                    ? t('Published release')
                    : dirty
                      ? t('Unsaved changes')
                      : draft
                        ? t('Draft saved')
                        : t('New set'),
                }}
              />
            </p>
          </div>
          {!onDirtyChange && <Link to="/sets/mine">{t('My sets')}</Link>}
        </header>
        {error && (
          <p className="notice" role="alert">
            {displayMessage(error)}
          </p>
        )}
        <fieldset disabled={busy || published} className="set-metadata">
          <label>
            {t('Title')}
            <input
              maxLength={128}
              value={pack.title}
              onChange={(e) => change({ ...pack, title: e.target.value })}
            />
          </label>
          <label>
            {t('Description')}
            <textarea
              maxLength={2000}
              value={pack.description}
              onChange={(e) => change({ ...pack, description: e.target.value })}
            />
          </label>
          <label>
            {t('Tags')}
            <input
              value={tagsText}
              onChange={(e) => {
                setTagsText(e.target.value);
                change({
                  ...pack,
                  tags: [
                    ...new Set(
                      e.target.value
                        .split(',')
                        .map((x) => x.trim())
                        .filter(Boolean),
                    ),
                  ].slice(0, 8),
                });
              }}
            />
          </label>
          <label>
            {t('License')}
            <select
              value={pack.license}
              onChange={(e) =>
                change({
                  ...pack,
                  license: e.target.value as SetPackage['license'],
                  credits: pack.credits.map((c, i) =>
                    i === 0 ? { ...c, license: e.target.value as SetPackage['license'] } : c,
                  ),
                })
              }
            >
              <option value="CC-BY-4.0">{t('CC BY 4.0 — credit required')}</option>
              <option value="CC0-1.0">{t('CC0 — public domain')}</option>
            </select>
          </label>
          <label>
            {t('Creator credit')}
            <input
              value={pack.credits[0]?.author ?? ''}
              onChange={(e) =>
                change({
                  ...pack,
                  credits: [
                    {
                      ...pack.credits[0],
                      author: e.target.value,
                      license: pack.credits[0]?.license ?? pack.license,
                    },
                    ...pack.credits.slice(1),
                  ],
                })
              }
            />
          </label>
          <details>
            <summary>{t('Additional credits and source attribution')}</summary>
            <JsonControl
              id="credits"
              value={pack.credits}
              onChange={(v) => {
                if (
                  !Array.isArray(v) ||
                  !v.length ||
                  v.length > 64 ||
                  v.some(
                    (c) =>
                      !c ||
                      typeof c !== 'object' ||
                      typeof c.author !== 'string' ||
                      !['CC0-1.0', 'CC-BY-4.0'].includes(c.license),
                  )
                )
                  throw Error('Enter an array of author credits.');
                change({ ...pack, credits: v as SetPackage['credits'] });
              }}
            />
          </details>
        </fieldset>
        <div className="set-workbench">
          <aside>
            <div className="set-tabs">
              <button
                aria-pressed={kind === 'terrain'}
                onClick={() => {
                  setKind('terrain');
                  setSelected(0);
                }}
              >
                {t('Terrain')}
              </button>
              <button
                aria-pressed={kind === 'resource'}
                onClick={() => {
                  setKind('resource');
                  setSelected(0);
                }}
              >
                {t('Resources')}
              </button>
            </div>
            <label>
              {t('Start from a preset')}
              <select
                value={kind === 'terrain' ? preset : resourcePreset}
                onChange={(e) =>
                  kind === 'terrain' ? setPreset(e.target.value) : setResourcePreset(e.target.value)
                }
              >
                {(kind === 'terrain' ? TERRAIN_PRESETS : RESOURCE_PRESETS.map((r) => r.key)).map(
                  (p) => (
                    <option key={p}>{p}</option>
                  ),
                )}
              </select>
            </label>
            <button disabled={busy || published} onClick={addEntry}>
              <RichMessage source={'Add {slot0}'} slots={{ slot0: kind }} />
            </button>
            <ul className="set-entry-list">
              {entries.map((e, i) => (
                <li key={String(e['key'])}>
                  <button aria-pressed={selected === i} onClick={() => setSelected(i)}>
                    {String(kind === 'terrain' ? e['name'] : object(e['presentation'])['name'])}
                  </button>
                </li>
              ))}
            </ul>
          </aside>
          <div className="set-entry-editor">
            {entry ? (
              <fieldset disabled={busy || published}>
                <legend>
                  <RichMessage
                    source={'{slot0} properties'}
                    slots={{ slot0: kind === 'terrain' ? t('Terrain') : t('Resource') }}
                  />
                </legend>
                <label>
                  {t('Name')}
                  <input
                    value={String(kind === 'terrain' ? entry['name'] : presentation['name'])}
                    onChange={(e) =>
                      kind === 'terrain'
                        ? updateEntry({ ...entry, name: e.target.value })
                        : updateEntry({
                            ...entry,
                            presentation: { ...presentation, name: e.target.value },
                          })
                    }
                  />
                </label>
                {kind === 'terrain' ? (
                  <>
                    <label>
                      {t('Gameplay preset')}
                      <select
                        value={String(entry['base'])}
                        onChange={(e) => updateEntry({ ...entry, base: e.target.value })}
                      >
                        {TERRAIN_PRESETS.map((p) => (
                          <option key={p}>{p}</option>
                        ))}
                      </select>
                    </label>
                    <p>{t('Properties inherit this preset until you add an override.')}</p>
                    <ObjectControls
                      idPrefix={String(entry['key']) + '/properties'}
                      value={object(entry['properties'])}
                      onChange={(v) => updateEntry({ ...entry, properties: v })}
                    />
                    <label>
                      {t('Add a property override')}
                      <select
                        defaultValue=""
                        onChange={(e) => {
                          if (e.target.value)
                            updateEntry({
                              ...entry,
                              properties: {
                                ...object(entry['properties']),
                                [e.target.value]: TERRAIN_PROPERTIES[e.target.value],
                              },
                            });
                          e.target.value = '';
                        }}
                      >
                        <option value="">{t('Choose property…')}</option>
                        {Object.keys(TERRAIN_PROPERTIES)
                          .filter((k) => !(k in object(entry['properties'])))
                          .map((p) => (
                            <option key={p} value={p}>
                              {name(p)}
                            </option>
                          ))}
                      </select>
                    </label>
                    <details>
                      <summary>{t('Resource placement permissions')}</summary>
                      <label>
                        {t('Allowed resource keys (null uses habitats)')}
                        <JsonControl
                          id={String(entry['key']) + '/allowedResources'}
                          value={entry['allowedResourceKeys'] ?? null}
                          onChange={(v) => updateEntry({ ...entry, allowedResourceKeys: v })}
                        />
                      </label>
                    </details>
                  </>
                ) : (
                  <>
                    <ObjectControls
                      idPrefix={String(entry['key']) + '/properties'}
                      value={object(entry['properties'])}
                      onChange={(v) => updateEntry({ ...entry, properties: v })}
                    />
                    <h3>{t('Material yields')}</h3>
                    {MATERIALS.map((m) => {
                      const yields = object(entry['yields']),
                        enabled = m in yields;
                      return (
                        <div key={m}>
                          <label>
                            <input
                              type="checkbox"
                              checked={enabled}
                              onChange={(e) => {
                                const next = { ...yields };
                                if (e.target.checked)
                                  next[m] = {
                                    capacity: 8,
                                    initial: 1,
                                    seedReserve: 1,
                                    growthRate: 196608,
                                    consumption: 'one',
                                    destroysDeposit: false,
                                    placementMaximum: 1,
                                  };
                                else Reflect.deleteProperty(next, m);
                                updateEntry({ ...entry, yields: next });
                              }}
                            />
                            {name(m)}
                          </label>
                          {enabled && (
                            <ObjectControls
                              idPrefix={String(entry['key']) + '/yields/' + m}
                              value={object(yields[m])}
                              onChange={(v) =>
                                updateEntry({ ...entry, yields: { ...yields, [m]: v } })
                              }
                            />
                          )}
                        </div>
                      );
                    })}
                  </>
                )}
                <h3>{t('Appearance')}</h3>
                <label>
                  {t('Spritesheet')}
                  <select
                    value={String(
                      kind === 'terrain'
                        ? (material['sprite'] ?? '')
                        : (presentation['sprite'] ?? ''),
                    )}
                    onChange={(e) => {
                      const path = e.target.value;
                      setSheet(path.replace('data/sets/', ''));
                      if (!path && kind === 'terrain') {
                        const next = structuredClone(pack);
                        Reflect.deleteProperty(next.assets.terrains, String(entry['key']));
                        change(next);
                        return;
                      }
                      if (kind === 'terrain')
                        updateMaterial({
                          ...material,
                          sprite: path,
                          profile: material['profile'] ?? 'soft',
                          preview: material['preview'] ?? [100, 130, 80],
                          variants: material['variants'] ?? [{ frame: 0, weight: 1 }],
                        });
                      else
                        updateEntry({ ...entry, presentation: { ...presentation, sprite: path } });
                    }}
                  >
                    <option value="" disabled={kind === 'resource'}>
                      {t('Choose uploaded sheet…')}
                    </option>
                    {kind === 'resource' &&
                      typeof presentation['sprite'] === 'string' &&
                      !presentation['sprite'].startsWith('data/sets/') && (
                        <option value={presentation['sprite']}>
                          {t('Installed preset artwork')}
                        </option>
                      )}
                    {pack.assets.sheets.map((s) => (
                      <option key={s.hash} value={'data/sets/' + s.hash}>
                        <RichMessage
                          source={'{slot0} · {slot1}×{slot2}'}
                          slots={{
                            slot0: s.hash.slice(0, 10),
                            slot1: s.frameWidth,
                            slot2: s.frameHeight,
                          }}
                        />
                      </option>
                    ))}
                  </select>
                </label>
                {kind === 'terrain' && !!material['sprite'] && (
                  <>
                    <label>
                      {t('Boundary style')}
                      <select
                        value={String(material['profile'] ?? 'soft')}
                        onChange={(e) => updateMaterial({ ...material, profile: e.target.value })}
                      >
                        {['soft', 'sand', 'fractured', 'rock', 'cobblestone', 'brush'].map((p) => (
                          <option key={p}>{p}</option>
                        ))}
                      </select>
                    </label>
                    <Variants
                      value={material['variants']}
                      frame={frame}
                      onChange={(v) => updateMaterial({ ...material, variants: v })}
                    />
                    <ObjectControls
                      idPrefix={String(entry['key']) + '/animation'}
                      value={{
                        animation_frames: material['animation_frames'] ?? 1,
                        animation_ticks: material['animation_ticks'] ?? 8,
                        animation_stride: material['animation_stride'] ?? 1,
                      }}
                      onChange={(v) => updateMaterial({ ...material, ...v })}
                    />
                    <details>
                      <summary>{t('Colors, seams and raised decor')}</summary>
                      <JsonControl
                        id={String(entry['key']) + '/material'}
                        value={material}
                        onChange={(v) => updateMaterial(object(v))}
                      />
                    </details>
                  </>
                )}
                {kind === 'resource' && (
                  <>
                    <h4>{t('Stock levels')}</h4>
                    {(Array.isArray(presentation['levels']) ? presentation['levels'] : []).map(
                      (l: unknown, i: number) => {
                        const level = object(l),
                          levels = presentation['levels'] as unknown[];
                        return (
                          <div key={i}>
                            <label>
                              {t('Minimum total stock')}
                              <input
                                type="number"
                                min={0}
                                value={Number(level['stock'])}
                                onChange={(e) =>
                                  updateEntry({
                                    ...entry,
                                    presentation: {
                                      ...presentation,
                                      levels: levels.map((x, j) =>
                                        i === j ? { ...level, stock: Number(e.target.value) } : x,
                                      ),
                                    },
                                  })
                                }
                              />
                            </label>
                            <Variants
                              value={level['variants']}
                              frame={frame}
                              onChange={(v) =>
                                updateEntry({
                                  ...entry,
                                  presentation: {
                                    ...presentation,
                                    levels: levels.map((x, j) =>
                                      i === j ? { ...level, variants: v } : x,
                                    ),
                                  },
                                })
                              }
                            />
                            {i > 0 && (
                              <button
                                onClick={() =>
                                  updateEntry({
                                    ...entry,
                                    presentation: {
                                      ...presentation,
                                      levels: levels.filter((_, j) => i !== j),
                                    },
                                  })
                                }
                              >
                                {t('Remove stock level')}
                              </button>
                            )}
                          </div>
                        );
                      },
                    )}
                    <button
                      onClick={() => {
                        const levels = Array.isArray(presentation['levels'])
                          ? presentation['levels']
                          : [];
                        updateEntry({
                          ...entry,
                          presentation: {
                            ...presentation,
                            levels: [
                              ...levels,
                              {
                                stock: Number(object(levels.at(-1))['stock'] ?? 0) + 1,
                                variants: [{ frame, weight: 1 }],
                              },
                            ],
                          },
                        });
                      }}
                    >
                      {t('Add stock level')}
                    </button>
                    <ObjectControls
                      idPrefix={String(entry['key']) + '/animation'}
                      value={{
                        animationFrames: presentation['animationFrames'] ?? 1,
                        animationTicks: presentation['animationTicks'] ?? 8,
                        animationStride: presentation['animationStride'] ?? 1,
                        minimap: presentation['minimap'] ?? [100, 130, 80],
                      }}
                      onChange={(v) =>
                        updateEntry({ ...entry, presentation: { ...presentation, ...v } })
                      }
                    />
                  </>
                )}
                <button
                  onClick={() => {
                    const next = structuredClone(pack),
                      key = String(entry['key']);
                    if (kind === 'terrain') {
                      next.terrains.splice(selected, 1);
                      Reflect.deleteProperty(next.assets.terrains, key);
                    } else next.resources.splice(selected, 1);
                    change(next);
                    setJsonEdits((previous) =>
                      Object.fromEntries(
                        Object.entries(previous).filter(([id]) => !id.startsWith(key + '/')),
                      ),
                    );
                    setSelected(0);
                  }}
                >
                  {t('Remove entry')}
                </button>
              </fieldset>
            ) : (
              <p>
                {t(
                  'Add a terrain or resource to begin. Built-in presets supply properties without copying their artwork into your set.',
                )}
              </p>
            )}
          </div>
          <aside>
            <h2>{t('Spritesheets')}</h2>
            <fieldset disabled={busy || published}>
              <div className="set-grid-size">
                <label>
                  {t('Frame width')}
                  <input
                    type="number"
                    min={1}
                    max={64}
                    value={frameWidth}
                    onChange={(e) => setFrameWidth(Number(e.target.value))}
                  />
                </label>
                <label>
                  {t('Frame height')}
                  <input
                    type="number"
                    min={1}
                    max={64}
                    value={frameHeight}
                    onChange={(e) => setFrameHeight(Number(e.target.value))}
                  />
                </label>
              </div>
              <p>
                {t(
                  'Terrain frames are 32×32. Resource and decor frames can be up to 64×64. Reupload the same PNG with new dimensions to replace its frame grid; then check its frame mappings.',
                )}
              </p>
              <label>
                {t('Upload PNG')}
                <input
                  type="file"
                  accept="image/png"
                  onChange={(e) => {
                    const file = e.target.files?.[0];
                    if (file)
                      void action(async () => {
                        const added = await sheetFromFile(file, frameWidth, frameHeight);
                        const existing = pack.assets.sheets.some((s) => s.hash === added.hash);
                        change({
                          ...pack,
                          assets: {
                            ...pack.assets,
                            sheets: existing
                              ? pack.assets.sheets.map((s) => (s.hash === added.hash ? added : s))
                              : [...pack.assets.sheets, added],
                          },
                        });
                        setSheet(added.hash);
                        setFrame(0);
                      });
                    e.target.value = '';
                  }}
                />
              </label>
            </fieldset>
            <label>
              {t('Inspect sheet')}
              <select
                value={sheet}
                onChange={(e) => {
                  setSheet(e.target.value);
                  setFrame(0);
                }}
              >
                <option value="">{t('Choose sheet…')}</option>
                {pack.assets.sheets.map((s) => (
                  <option key={s.hash} value={s.hash}>
                    {s.hash.slice(0, 10)}
                  </option>
                ))}
              </select>
            </label>
            {activeSheet && (
              <div className="set-sheet">
                <img
                  src={sheetUrl(activeSheet.png)}
                  alt={t('Uploaded spritesheet; click a tile to select its frame')}
                  onClick={(e) => {
                    const img = e.currentTarget,
                      rect = img.getBoundingClientRect(),
                      x = Math.min(
                        img.naturalWidth - 1,
                        Math.floor(((e.clientX - rect.left) * img.naturalWidth) / rect.width),
                      ),
                      y = Math.min(
                        img.naturalHeight - 1,
                        Math.floor(((e.clientY - rect.top) * img.naturalHeight) / rect.height),
                      );
                    setFrame(
                      Math.floor(y / activeSheet.frameHeight) *
                        (img.naturalWidth / activeSheet.frameWidth) +
                        Math.floor(x / activeSheet.frameWidth),
                    );
                  }}
                />
                <label>
                  {t('Selected frame')}
                  <input
                    type="number"
                    min={0}
                    value={frame}
                    onChange={(e) => setFrame(Number(e.target.value))}
                  />
                </label>
                <small>{t('Frames run left to right, then top to bottom.')}</small>
                <button
                  disabled={busy || published}
                  onClick={() => {
                    const path = 'data/sets/' + activeSheet.hash;
                    if (
                      JSON.stringify([
                        pack.terrains,
                        pack.resources,
                        pack.assets.terrains,
                      ]).includes(path)
                    ) {
                      setError(
                        'This sheet is used by an entry. Change its spritesheet or remove that entry before removing the sheet.',
                      );
                      return;
                    }
                    change({
                      ...pack,
                      assets: {
                        ...pack.assets,
                        sheets: pack.assets.sheets.filter((s) => s.hash !== activeSheet.hash),
                      },
                    });
                    setSheet('');
                    setFrame(0);
                  }}
                >
                  {t('Remove sheet')}
                </button>
              </div>
            )}
          </aside>
        </div>
        <SetPreview pack={pack} />
        <footer className="set-validation">
          <h2>{t('Check & publish')}</h2>
          <p>
            {t(
              'Checks validate gameplay properties, image bounds, frame mappings and rendering using the game engine. They do not establish map balance.',
            )}
          </p>
          <div className="set-actions">
            <button
              disabled={busy || published}
              onClick={() =>
                void action(async () => {
                  const current = await save();
                  if (!id) navigate('/sets/drafts/' + current.id, { replace: true });
                })
              }
            >
              {t('Save draft')}
            </button>
            <button
              disabled={busy || published || draft?.validation?.status === 'pending'}
              onClick={() =>
                void action(async () => {
                  const current = dirty || !draft ? await save() : draft;
                  const checked = await request<SetDraft>(
                    'POST',
                    `/api/v1/set-drafts/${current.id}/validate`,
                    {
                      body: { revision: current.revision },
                    },
                  );
                  setDraft(checked);
                  // Navigate only after the operation completes; the keyed replacement workspace
                  // must load the pending validation rather than lose an unmounted state update.
                  if (!id) navigate('/sets/drafts/' + current.id, { replace: true });
                })
              }
            >
              {t('Run checks & preview')}
            </button>
          </div>
          {draft?.validation && (
            <p role="status">
              {dirty ? t('Checks apply to the last saved revision. ') : ''}
              {draft.validation.status === 'valid'
                ? t('Checks passed')
                : draft.validation.status === 'pending'
                  ? t('Checking your set…')
                  : (draft.validation.report?.reason ??
                    draft.validation.error ??
                    t('Checks failed'))}
            </p>
          )}
          {draft?.validation?.report?.previewHash && (
            <img
              className="set-contact"
              src={`/api/v1/set-drafts/${draft.id}/preview?revision=${draft.revision}`}
              alt={t('Game-engine preview of the checked set')}
            />
          )}
          <button onClick={() => setReleaseOpen(true)}>
            <Icon name="share" size={18} /> {t(' Review & publish')}
          </button>
          {!checked && <p>{t('Save and run checks on the current revision before publishing.')}</p>}
          <ReleaseDialog
            open={releaseOpen}
            onClose={() => setReleaseOpen(false)}
            title={t('Publish revision {value0}', { value0: draft?.revision ?? 'unsaved' })}
          >
            <fieldset disabled={!checked || busy || published}>
              <label>
                {t('Release label')}
                <input value={label} maxLength={64} onChange={(e) => setLabel(e.target.value)} />
              </label>
              <label>
                {t('Release notes')}
                <textarea
                  value={notes}
                  maxLength={2000}
                  onChange={(e) => setNotes(e.target.value)}
                />
              </label>
              <label>
                {t('Visibility')}
                <select value={visibility} onChange={(e) => setVisibility(e.target.value)}>
                  <option value="public">{t('Public library')}</option>
                  <option value="unlisted">{t('Anyone with the link')}</option>
                  <option value="private">{t('Only me')}</option>
                </select>
              </label>
              <p>
                <RichMessage
                  source={
                    'Publishing allows reuse under {slot0}, including embedding and editing this content in shared maps. Confirm that the credits and reuse rights cover every uploaded image.'
                  }
                  slots={{ slot0: pack.license }}
                />
              </p>
              <button
                onClick={() =>
                  void action(async () => {
                    if (!draft) throw Error('Save the draft before publishing.');
                    const published = await request<SetInfo>(
                      'POST',
                      `/api/v1/set-drafts/${draft.id}/publish`,
                      { body: { revision: draft.revision, label, notes, visibility } },
                    );
                    navigate('/sets/' + published.id);
                  })
                }
              >
                {t('Publish this release')}
              </button>
            </fieldset>
          </ReleaseDialog>
        </footer>
      </section>
    </JsonEdits.Provider>
  );
}

/** Standalone route uses the same editor as Terrain Studio. */
export function SetWorkspace(props: Parameters<typeof SetEditor>[0]) {
  useLocale();
  return <SetEditor {...props} />;
}
