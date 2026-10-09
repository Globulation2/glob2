import { MessageError } from '../i18n.tsx';
import { displayMessage, message as sourceMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { ReleaseDialog } from '../components/studio/Studio.tsx';
import { Icon } from '../icons.tsx';
import { useEffect, useRef, useState } from 'react';
import {
  buildingNamespacePrefix,
  checkBuildingPackage,
  type BuildingFamily,
  type BuildingDraft,
  type BuildingDraftList,
  type BuildingPackage,
} from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded, ErrorNotice } from '../components/common.tsx';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import '../styles/buildings.css';
const path = (id: string) => '/api/v1/building-drafts/' + id;
const fail = (value: unknown) => (value instanceof Error ? value : new Error(String(value)));
const json = (value: unknown) => JSON.stringify(value, null, 2);

/** Scalars have native controls; nested capabilities retain lossless JSON editing. */
function Fields({
  value,
  onChange,
  edits,
  editPrefix,
  onEdit,
}: {
  edits: Record<string, string>;
  editPrefix: string;
  onEdit: (key: string, text: string) => void;
  value: Record<string, unknown>;
  onChange: (value: Record<string, unknown>) => void;
}) {
  useLocale();
  const [newKey, setNewKey] = useState('');
  return (
    <div className="building-fields">
      {Object.entries(value).map(([key, item]) => (
        <div className="building-field" key={key}>
          <label>
            {key}
            {typeof item === 'boolean' ? (
              <input
                type="checkbox"
                checked={item}
                onChange={(e) => onChange({ ...value, [key]: e.target.checked })}
              />
            ) : typeof item === 'number' ? (
              <input
                type="number"
                value={item}
                onChange={(e) => {
                  if (e.target.value !== '' && Number.isFinite(e.target.valueAsNumber))
                    onChange({ ...value, [key]: e.target.valueAsNumber });
                }}
              />
            ) : typeof item === 'string' ? (
              <input value={item} onChange={(e) => onChange({ ...value, [key]: e.target.value })} />
            ) : (
              <JsonField
                key={key}
                value={item}
                text={edits[editPrefix + key]}
                onText={(text) => onEdit(editPrefix + key, text)}
                onChange={(next) => onChange({ ...value, [key]: next })}
              />
            )}
          </label>
          <button
            type="button"
            className="small"
            aria-label={t('Remove {value0}', { value0: key })}
            onClick={() => {
              onChange(Object.fromEntries(Object.entries(value).filter(([name]) => name !== key)));
            }}
          >
            {t('Remove')}
          </button>
        </div>
      ))}
      <div className="building-field">
        <label>
          {t('Additional field')}
          <input value={newKey} onChange={(e) => setNewKey(e.target.value)} />
        </label>
        <button
          type="button"
          disabled={
            !newKey.trim() ||
            Object.hasOwn(value, newKey.trim()) ||
            ['__proto__', 'constructor', 'prototype'].includes(newKey.trim())
          }
          onClick={() => {
            onChange({ ...value, [newKey.trim()]: null });
            setNewKey('');
          }}
        >
          {t('Add')}
        </button>
      </div>
    </div>
  );
}
function JsonField({
  value,
  onChange,
  label = t('JSON value'),
  kind,
  text: pendingText,
  onText,
}: {
  text?: string;
  onText: (text: string) => void;
  value: unknown;
  onChange: (value: unknown) => void;
  label?: string;
  kind?: 'array';
}) {
  useLocale();
  const text = pendingText ?? json(value);
  const [error, setError] = useState('');
  const parseValue = (value: string) => {
    const parsed: unknown = JSON.parse(value);
    if (kind === 'array' && !Array.isArray(parsed)) throw new MessageError('Enter a JSON array.');
    return parsed;
  };
  return (
    <>
      <textarea
        dir="ltr"
        rows={5}
        ref={(element) => {
          if (!element) return;
          try {
            parseValue(text);
            element.setCustomValidity('');
          } catch {
            element.setCustomValidity(t('Enter valid JSON.'));
          }
        }}
        aria-label={label}
        spellCheck={false}
        value={text}
        onChange={(e) => {
          onText(e.target.value);
          try {
            parseValue(e.target.value);
            e.currentTarget.setCustomValidity('');
            setError('');
          } catch {
            e.currentTarget.setCustomValidity(
              kind === 'array' ? 'Enter a valid JSON array.' : 'Enter valid JSON.',
            );
            setError(kind === 'array' ? 'Enter a valid JSON array.' : 'Enter valid JSON.');
          }
        }}
        onBlur={() => {
          try {
            onChange(parseValue(text));
            setError('');
          } catch {
            setError('Enter valid JSON.');
          }
        }}
      />
      {error && <span role="alert">{displayMessage(error)}</span>}
    </>
  );
}
export function BuildingStudio({ id }: { id?: string }) {
  useLocale();
  const { account } = useSession(),
    { navigate } = useRouter();
  const [error, setError] = useState<Error>(),
    [busy, setBusy] = useState(false);
  const load = useLoad(
    (signal) => request<BuildingDraftList>('GET', '/api/v1/building-drafts', { signal }),
    [account?.id],
  );
  if (id) return <BuildingWorkspace key={id} id={id} />;
  return (
    <div className="building-studio">
      <h1>{t('Building Studio')}</h1>
      <p>{t('Create a building or an upgrade family with custom artwork.')}</p>
      <p>
        <RichMessage
          source={'Drafts are private. Publish a saved family to share it in the {slot0}.'}
          slots={{ slot0: <Link to="/buildings">{t('building library')}</Link> }}
        />
      </p>
      {!account ? (
        <p>
          <RichMessage
            source={'{slot0} to create and save drafts.'}
            slots={{ slot0: <a href="/signin">{t('Sign in')}</a> }}
          />
        </p>
      ) : (
        <>
          <button
            type="button"
            disabled={busy}
            onClick={async () => {
              setBusy(true);
              setError(undefined);
              try {
                const draft = await request<BuildingDraft>('POST', '/api/v1/building-drafts', {
                  body: {},
                });
                navigate('/building-studio/' + draft.id);
              } catch (e) {
                setError(fail(e));
              } finally {
                setBusy(false);
              }
            }}
          >
            {t('Create a family')}
          </button>
          {error && <ErrorNotice error={error} />}
          <Loaded load={load}>
            {(data) => (
              <ul>
                {data.items.map((draft) => (
                  <li key={draft.id}>
                    <Link to={'/building-studio/' + draft.id}>{draft.name}</Link>
                  </li>
                ))}
              </ul>
            )}
          </Loaded>
        </>
      )}
    </div>
  );
}
function BuildingWorkspace({ id }: { id: string }) {
  useLocale();
  const load = useLoad((signal) => request<BuildingDraft>('GET', path(id), { signal }), [id]);
  return (
    <Loaded load={load}>{(draft) => <BuildingEditor key={draft.id} initial={draft} />}</Loaded>
  );
}
export function BuildingEditor({
  initial,
  embedded = false,
  onDirtyChange,
  onSaved,
}: {
  initial: BuildingDraft;
  embedded?: boolean;
  onDirtyChange?: (dirty: boolean) => void;
  onSaved?: () => void;
}) {
  useLocale();
  const [releaseOpen, setReleaseOpen] = useState(false);
  const form = useRef<HTMLFormElement>(null);
  const allowNavigation = useRef(false);
  const { navigate } = useRouter();
  const [draft, setDraft] = useState(initial),
    [pkg, setPackage] = useState(initial.package),
    [name, setName] = useState(initial.name);
  const [selected, setSelected] = useState(0),
    [busy, setBusy] = useState(false),
    [error, setError] = useState<Error>(),
    [message, setMessage] = useState('');
  const [fieldEdits, setFieldEdits] = useState<Record<string, string>>({});
  const editField = (key: string, text: string) =>
    setFieldEdits((edits) => ({ ...edits, [key]: text }));
  const [raw, setRaw] = useState(json(initial.package)),
    [tab, setTab] = useState<'fields' | 'json'>('fields');
  const [sprite, setSprite] = useState('building'),
    [frame, setFrame] = useState(0),
    [layer, setLayer] = useState('image');
  const [local, setLocal] = useState<
    | { name: string; package: BuildingPackage; raw?: string; fieldEdits?: Record<string, string> }
    | undefined
  >(() => {
    try {
      const saved = localStorage.getItem('building-draft:' + initial.id);
      return saved ? JSON.parse(saved) : undefined;
    } catch {
      return undefined;
    }
  });
  const dirty =
    name !== draft.name ||
    json(pkg) !== json(draft.package) ||
    (tab === 'json' && raw !== json(pkg)) ||
    Object.keys(fieldEdits).length > 0;
  useEffect(() => {
    onDirtyChange?.(dirty);
  }, [dirty, onDirtyChange]);
  const variant = pkg.variants[selected];
  useEffect(() => {
    if (!dirty) return;
    try {
      localStorage.setItem(
        'building-draft:' + initial.id,
        json({ name, package: pkg, raw: tab === 'json' ? raw : undefined, fieldEdits }),
      );
    } catch {
      /* Storage may be full. */
    }
  }, [initial.id, name, pkg, dirty, tab, raw, fieldEdits]);
  useEffect(() => {
    if (!dirty) return;
    const leave = (event: BeforeUnloadEvent) => {
      event.preventDefault();
    };
    const leaveRoute = (event: Event) => {
      if (allowNavigation.current) return;
      if (!window.confirm(t('Leave with unsaved changes? A recovery copy stays on this device.'))) {
        event.preventDefault();
      }
    };
    window.addEventListener('beforeunload', leave);
    window.addEventListener('glob2-before-navigate', leaveRoute);
    return () => {
      window.removeEventListener('beforeunload', leave);
      window.removeEventListener('glob2-before-navigate', leaveRoute);
    };
  }, [dirty]);
  const accept = (next: BuildingDraft) => {
    setFieldEdits({});
    setDraft(next);
    setPackage(next.package);
    setRaw(json(next.package));
    setName(next.name);
    setSelected((n) => Math.min(n, next.package.variants.length - 1));
    setLocal(undefined);
    try {
      localStorage.removeItem('building-draft:' + next.id);
    } catch {
      /* Optional. */
    }
  };
  const operation = async (work: () => Promise<BuildingDraft>, success: string) => {
    setBusy(true);
    setError(undefined);
    setMessage('');
    try {
      accept(await work());
      setMessage(success);
      onSaved?.();
    } catch (e) {
      setError(fail(e));
    } finally {
      setBusy(false);
    }
  };
  const save = () => {
    if (!form.current?.reportValidity()) {
      setMessage(sourceMessage('Resolve invalid fields before saving.'));
      return;
    }
    return operation(async () => {
      // Hidden stage editors must also retain and validate their pending text.
      for (const text of Object.values(fieldEdits)) JSON.parse(text);
      const current =
        tab === 'json' ? checkBuildingPackage(JSON.parse(raw)) : checkBuildingPackage(pkg);
      return request<BuildingDraft>('PUT', path(draft.id), {
        body: { revision: draft.revision, name, package: current },
      });
    }, t('Draft saved.'));
  };
  const update = (changes: Partial<typeof variant>) => {
    if (!variant) return;
    setPackage({
      ...pkg,
      variants: pkg.variants.map((v, i) => (i === selected ? { ...v, ...changes } : v)),
    });
  };
  return (
    <div className="building-studio">
      {!embedded && (
        <>
          <Link to="/building-studio">{t('Your building drafts')}</Link>
          <h1>{t('Building family editor')}</h1>
          <Link to={'/ai-building-studio?draft=' + draft.id}>{t('Edit with AI')}</Link>
        </>
      )}
      {embedded && <h2>{t('Building family editor')}</h2>}
      <p>
        {t(
          'Start with one building, add stages for upgrades, then save and publish. Artwork uploads are saved immediately; choose their sprite keys in gameSprite and miniSprite.',
        )}
      </p>
      <p role="status">{message || (dirty ? t('Unsaved changes') : t('Account draft saved'))}</p>
      {error && <ErrorNotice error={error} />}
      {local && (
        <div className="notice">
          <p>{t('This device has a recovery copy.')}</p>
          <button
            type="button"
            onClick={() => {
              try {
                const recovered = checkBuildingPackage(local.package);
                setPackage(recovered);
                setRaw(local.raw ?? json(recovered));
                if (local.raw) setTab('json');
                setName(local.name);
                setFieldEdits(local.fieldEdits ?? {});
                setLocal(undefined);
              } catch (e) {
                setError(fail(e));
              }
            }}
          >
            {t('Restore device copy')}
          </button>
        </div>
      )}
      <button onClick={() => setReleaseOpen(true)}>
        <Icon name="share" size={18} /> {t(' Review release settings')}
      </button>
      {dirty && <p>{t('Save your changes before publishing.')}</p>}
      <ReleaseDialog
        open={releaseOpen}
        onClose={() => setReleaseOpen(false)}
        title={t('Publish saved revision {value0}', { value0: draft.revision })}
      >
        <p>{t('This release uses the current saved draft. Unsaved edits are not included.')}</p>
        <form
          onSubmit={(e) => {
            e.preventDefault();
            if (dirty || busy) return;
            const data = new FormData(e.currentTarget);
            setBusy(true);
            setError(undefined);
            void request<BuildingFamily>('POST', path(draft.id) + '/publish', {
              body: {
                revision: draft.revision,
                description: data.get('description'),
                visibility: data.get('visibility'),
              },
            })
              .then((f) => {
                window.location.assign('/buildings/' + f.id);
              })
              .catch((e) => setError(fail(e)))
              .finally(() => setBusy(false));
          }}
        >
          <label>
            {t('Description')}
            <textarea name="description" maxLength={4000} />
          </label>
          <label>
            {t('Visibility')}
            <select name="visibility" defaultValue="unlisted">
              <option value="unlisted">{t('Unlisted (people with the link)')}</option>
              <option value="public">{t('Public library')}</option>
              <option value="private">{t('Private')}</option>
            </select>
          </label>
          <button disabled={dirty || busy}>{t('Publish saved family')}</button>
          <p>
            {t(
              'Save changes first. Publishing creates a pinned release of the saved package. The engine checks it before people can download it. Private releases are visible only to you; unlisted releases can be opened with their link.',
            )}
          </p>
        </form>
      </ReleaseDialog>
      <form ref={form} onSubmit={(e) => e.preventDefault()}>
        <fieldset disabled={busy}>
          <label>
            {t('Family name')}
            <input value={name} maxLength={128} onChange={(e) => setName(e.target.value)} />
          </label>
          <div className="building-actions">
            <button type="button" onClick={save}>
              {t('Save draft')}
            </button>
            <a className="button" href={path(draft.id) + '/archive'}>
              {t('Export saved package')}
            </a>
            <label>
              {t('Import package')}
              <input
                type="file"
                accept=".zip,application/zip"
                disabled={dirty}
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  if (file)
                    void operation(
                      () =>
                        request<BuildingDraft>('PUT', path(draft.id) + '/archive', {
                          query: { revision: draft.revision },
                          body: file,
                        }),
                      'Package imported.',
                    );
                  e.target.value = '';
                }}
              />
            </label>
          </div>
          {dirty && (
            <p>
              {t(
                'Save your changes before importing or uploading artwork. Export uses the saved draft.',
              )}
            </p>
          )}
          <div className="building-actions">
            <button
              type="button"
              aria-pressed={tab === 'fields'}
              onClick={() => {
                try {
                  if (tab === 'json') setPackage(checkBuildingPackage(JSON.parse(raw)));
                  setTab('fields');
                } catch (e) {
                  setError(fail(e));
                }
              }}
            >
              {t('Fields')}
            </button>
            <button
              type="button"
              aria-pressed={tab === 'json'}
              onClick={() => {
                if (tab === 'json' || !form.current?.reportValidity()) return;
                try {
                  for (const text of Object.values(fieldEdits)) JSON.parse(text);
                } catch (e) {
                  setError(fail(e));
                  return;
                }
                setFieldEdits({});
                setRaw(json(pkg));
                setTab('json');
              }}
            >
              {t('JSON')}
            </button>
          </div>
          {tab === 'json' ? (
            <label>
              {t('Complete family JSON')}
              <textarea
                className="building-json"
                rows={28}
                spellCheck={false}
                value={raw}
                onChange={(e) => setRaw(e.target.value)}
              />
            </label>
          ) : (
            <>
              <h2>{t('Buildings and upgrade stages')}</h2>
              <label>
                {t('Stage')}
                <select value={selected} onChange={(e) => setSelected(Number(e.target.value))}>
                  {pkg.variants.map((v, i) => (
                    <option key={i} value={i}>
                      {v.key.slice(buildingNamespacePrefix(pkg.namespace).length)}
                    </option>
                  ))}
                </select>
              </label>
              <div className="building-actions">
                <button
                  type="button"
                  onClick={() => {
                    const key =
                      buildingNamespacePrefix(pkg.namespace) +
                      'building-' +
                      crypto.randomUUID().slice(0, 8);
                    setPackage({
                      ...pkg,
                      variants: [
                        ...pkg.variants,
                        {
                          ...structuredClone(variant ?? { properties: {}, semantics: {}, key }),
                          key,
                          previous: '',
                          next: '',
                        },
                      ],
                    });
                    setSelected(pkg.variants.length);
                  }}
                >
                  {t('Add stage')}
                </button>
                <button
                  type="button"
                  disabled={pkg.variants.length === 1}
                  onClick={() => {
                    if (!variant) return;
                    setFieldEdits((edits) =>
                      Object.fromEntries(
                        Object.entries(edits).filter(([key]) => !key.startsWith(variant.key + '/')),
                      ),
                    );
                    setPackage({
                      ...pkg,
                      variants: pkg.variants
                        .filter((_, i) => i !== selected)
                        .map((v) => ({
                          ...v,
                          previous: v.previous === variant.key ? '' : v.previous,
                          next: v.next === variant.key ? '' : v.next,
                        })),
                    });
                    setSelected(0);
                  }}
                >
                  {t('Remove stage')}
                </button>
              </div>
              {variant && (
                <>
                  <label>
                    {t('Stable key')}
                    <input
                      value={variant.key}
                      onChange={(e) => {
                        const old = variant.key,
                          key = e.target.value;
                        setFieldEdits((edits) =>
                          Object.fromEntries(
                            Object.entries(edits).map(([field, text]) => [
                              field.startsWith(old + '/') ? key + field.slice(old.length) : field,
                              text,
                            ]),
                          ),
                        );
                        setPackage({
                          ...pkg,
                          variants: pkg.variants.map((v, i) => ({
                            ...v,
                            ...(i === selected ? { key } : {}),
                            previous: v.previous === old ? key : v.previous,
                            next: v.next === old ? key : v.next,
                          })),
                        });
                      }}
                    />
                  </label>
                  {(['previous', 'next'] as const).map((field) => (
                    <label key={field}>
                      {field === 'previous' ? t('Previous stage') : t('Next stage')}
                      <select
                        value={variant[field] ?? ''}
                        onChange={(e) => update({ [field]: e.target.value })}
                      >
                        <option value="">{t('None')}</option>
                        {pkg.variants
                          .filter((v) => v.key !== variant.key)
                          .map((v) => (
                            <option key={v.key} value={v.key}>
                              {v.key.slice(buildingNamespacePrefix(pkg.namespace).length)}
                            </option>
                          ))}
                      </select>
                    </label>
                  ))}
                  <label>
                    {t('Required experiment')}
                    <select
                      value={variant.requiredExperiment ?? ''}
                      onChange={(e) => update({ requiredExperiment: e.target.value })}
                    >
                      <option value="">{t('Always available')}</option>
                      {pkg.experiments.map((e) => (
                        <option key={e.key} value={e.key}>
                          {e.label}
                        </option>
                      ))}
                    </select>
                  </label>
                  {(['properties', 'semantics', 'presentation'] as const).map((section) => (
                    <details key={section} open>
                      <summary>
                        {
                          {
                            properties: t('Properties'),
                            semantics: t('Semantics'),
                            presentation: t('Presentation'),
                          }[section]
                        }
                      </summary>
                      <Fields
                        key={variant.key + section}
                        value={variant[section] ?? {}}
                        edits={fieldEdits}
                        editPrefix={variant.key + '/' + section + '/'}
                        onEdit={editField}
                        onChange={(value) => update({ [section]: value })}
                      />
                    </details>
                  ))}
                </>
              )}
              <details>
                <summary>{t('Family experiments')}</summary>
                <JsonField
                  text={fieldEdits['experiments']}
                  onText={(text) => editField('experiments', text)}
                  kind="array"
                  label={t('Family experiments JSON')}
                  value={pkg.experiments}
                  onChange={(value) => {
                    if (Array.isArray(value))
                      setPackage({ ...pkg, experiments: value as BuildingPackage['experiments'] });
                  }}
                />
              </details>
            </>
          )}
          <h2>{t('Artwork')}</h2>
          <p>
            {t(
              'Upload still PNG or WebP frames up to 512 × 512. Team-color layers use the same dimensions as the base frame.',
            )}
          </p>
          <div className="building-actions">
            <label>
              {t('Sprite key')}
              <input value={sprite} onChange={(e) => setSprite(e.target.value)} />
            </label>
            <label>
              {t('Frame')}
              <input
                type="number"
                min={0}
                max={255}
                value={frame}
                onChange={(e) => setFrame(e.target.valueAsNumber)}
              />
            </label>
            <label>
              {t('Layer')}
              <select value={layer} onChange={(e) => setLayer(e.target.value)}>
                <option value="image">{t('Base artwork')}</option>
                <option value="team">{t('Team color')}</option>
              </select>
            </label>
            <label>
              {t('Upload frame')}
              <input
                type="file"
                accept="image/png,image/webp"
                disabled={dirty}
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  if (file)
                    void operation(
                      () =>
                        request<BuildingDraft>('PUT', path(draft.id) + '/frame', {
                          query: { revision: draft.revision, sprite, frame, layer },
                          body: file,
                        }),
                      'Artwork saved.',
                    );
                  e.target.value = '';
                }}
              />
            </label>
          </div>
          <p>
            <RichMessage
              source={'Use {slot0} in gameSprite or miniSprite to select an uploaded sprite.'}
              slots={{
                slot0: (
                  <code>
                    {t('package:')}
                    {sprite}
                  </code>
                ),
              }}
            />
          </p>
          <div className="building-frames">
            {draft.package.sprites.flatMap((s) =>
              s.frames.map((f, i) => (
                <figure key={s.key + i}>
                  <img
                    width={Math.min(128, f.width)}
                    height={Math.min(128, f.height)}
                    src={path(draft.id) + '/assets/' + f.imageHash}
                    alt={t('{value0}, frame {value1}', { value0: s.key, value1: i })}
                  />
                  {f.teamColorHash && (
                    <img
                      width={Math.min(128, f.width)}
                      height={Math.min(128, f.height)}
                      src={path(draft.id) + '/assets/' + f.teamColorHash}
                      alt={t('{value0}, frame {value1}, team color layer', {
                        value0: s.key,
                        value1: i,
                      })}
                    />
                  )}
                  <figcaption>
                    <RichMessage
                      source={'{slot0} · {slot1} · {slot2} × {slot3}'}
                      slots={{ slot0: s.key, slot1: i, slot2: f.width, slot3: f.height }}
                    />
                  </figcaption>
                </figure>
              )),
            )}
          </div>
          <details>
            <summary>{t('Sprite manifest')}</summary>
            <JsonField
              text={fieldEdits['sprites']}
              onText={(text) => editField('sprites', text)}
              kind="array"
              label={t('Sprite manifest JSON')}
              value={pkg.sprites}
              onChange={(value) => {
                if (Array.isArray(value))
                  setPackage({ ...pkg, sprites: value as BuildingPackage['sprites'] });
              }}
            />
          </details>
          <button
            type="button"
            onClick={async () => {
              if (
                !window.confirm(
                  `Delete “${name}”? This removes the account draft and this device’s recovery copy. Published releases remain available.`,
                )
              )
                return;
              setBusy(true);
              try {
                await request('DELETE', path(draft.id));
                try {
                  localStorage.removeItem('building-draft:' + draft.id);
                } catch {
                  /* Device storage can be disabled. */
                }
                // Successful deletion already confirmed discarding this editor.
                allowNavigation.current = true;
                navigate('/building-studio');
              } catch (e) {
                setError(fail(e));
                setBusy(false);
              }
            }}
          >
            {t('Delete draft')}
          </button>
        </fieldset>
      </form>
    </div>
  );
}
