import { displayMessage } from '../i18n.tsx';
import { translateError } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import {
  LibraryHeader,
  LibraryNav,
  LibraryFilters,
  LibraryField,
  LibraryGrid,
  LibraryCard,
  LibraryResults,
  LibraryEmpty,
  useLibrarySearch,
} from '../components/library.tsx';
import { GameArt } from '../art.tsx';
import { useState } from 'react';
import type { SetInfo, SetList, SetDraft, SetDraftSummary, SetPackage } from '@glob2/protocol';
import { request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession, isModerator } from '../state.tsx';
import { newRelease } from './model.ts';
import './sets.css';

export function SetLibrary({ mine = false }: { mine?: boolean }) {
  useLocale();
  const { account } = useSession();
  const [q, setQ] = useState(''),
    [sort, setSort] = useState('newest'),
    [kind, setKind] = useState(''),
    [license, setLicense] = useState('');
  const [query, setQuery] = useState('');
  const [cursor, setCursor] = useState('');
  const [draftCursor, setDraftCursor] = useState(''),
    [draftPages, setDraftPages] = useState<SetDraftSummary[]>([]);
  const data = useLoad(
    (signal) =>
      request<SetList>('GET', '/api/v1/sets', {
        query: { q: query, sort, kind, license, owner: mine ? 'me' : undefined, cursor },
        signal,
      }),
    [query, sort, kind, license, mine, cursor, account?.id],
  );
  const drafts = useLoad(
    (signal) =>
      mine
        ? request<{ items: SetDraftSummary[]; nextCursor?: string }>('GET', '/api/v1/set-drafts', {
            signal,
            query: { cursor: draftCursor, limit: 100 },
          })
        : Promise.resolve({ items: [] }),
    [mine, draftCursor],
  );
  const draftItems = [
    ...draftPages,
    ...(drafts.status === 'ready' ? drafts.data.items : []),
  ].filter(
    (d, i, all) => all.findIndex((other) => other.id === d.id) === i && !d.publishedVersionId,
  );
  const applySearch = useLibrarySearch(q, query, (value) => {
    setQuery(value);
    setCursor('');
  });
  const reset = () => {
    setQ('');
    setQuery('');
    setKind('');
    setLicense('');
    setSort('newest');
    setCursor('');
  };
  const filtered = Boolean(query || kind || license);
  return (
    <section className="set-library library-page">
      <LibraryHeader
        art="wood"
        title={mine ? t('My sets') : t('Terrain & resource sets')}
        description={t('Build a consistent world with custom terrain, resources, and artwork.')}
        actions={
          <>
            <Link className="btn primary" to="/sets/new">
              {t('Create a set')}
            </Link>
            <Link className="btn" to="/terrain-studio">
              {t('Create with AI')}
            </Link>
          </>
        }
      />
      <LibraryNav label={t('Terrain & resource sets')}>
        <Link to="/sets" className={mine ? '' : 'on'} aria-current={mine ? undefined : 'page'}>
          {t('Browse')}
        </Link>
        <Link to="/sets/mine" className={mine ? 'on' : ''} aria-current={mine ? 'page' : undefined}>
          {t('My sets')}
        </Link>
      </LibraryNav>
      {isModerator(account) && <Link to="/sets/reports">{t('Moderation reports')}</Link>}
      <LibraryFilters
        onSubmit={(event) => {
          event.preventDefault();
          applySearch();
        }}
      >
        <LibraryField label={t('Search')} search>
          <input
            type="search"
            value={q}
            onChange={(e) => {
              setQ(e.target.value);
            }}
            placeholder={t('Theme, title or creator')}
          />
        </LibraryField>
        <LibraryField label={t('Contents')}>
          <select
            value={kind}
            onChange={(e) => {
              setKind(e.target.value);
              setCursor('');
            }}
          >
            <option value="">{t('All sets')}</option>
            <option value="terrain">{t('Terrain')}</option>
            <option value="resource">{t('Resources')}</option>
            <option value="both">{t('Both')}</option>
          </select>
        </LibraryField>
        <LibraryField label={t('License')}>
          <select
            value={license}
            onChange={(e) => {
              setLicense(e.target.value);
              setCursor('');
            }}
          >
            <option value="">{t('All licenses')}</option>
            <option value="CC0-1.0">{t('CC0')}</option>
            <option value="CC-BY-4.0">{t('CC BY')}</option>
          </select>
        </LibraryField>
        <LibraryField label={t('Sort')}>
          <select
            value={sort}
            onChange={(e) => {
              setSort(e.target.value);
              setCursor('');
            }}
          >
            {[
              ['newest', t('Newest')],
              ['updated', t('Recently updated')],
              ['likes', t('Most liked')],
              ['downloads', t('Most downloaded')],
            ].map(([value, label]) => (
              <option key={value} value={value}>
                {label}
              </option>
            ))}
          </select>
        </LibraryField>
        <button type="button" onClick={reset}>
          {t('Reset filters')}
        </button>
      </LibraryFilters>
      {mine && (
        <div className="set-drafts" aria-label={t('Your drafts')}>
          <h2>{t('Drafts')}</h2>
          {draftItems.map((d) => (
            <Link key={d.id} to={'/sets/drafts/' + d.id}>
              <RichMessage source={'{slot0} · Continue editing'} slots={{ slot0: d.title }} />
            </Link>
          ))}
          {drafts.status === 'loading' && <p role="status">{t('Loading drafts…')}</p>}
          {drafts.status === 'error' && <p role="alert">{translateError(drafts.error)}</p>}
          {drafts.status === 'ready' && !draftItems.length && <p>{t('No unpublished drafts.')}</p>}
          {drafts.status === 'ready' && drafts.data.nextCursor && (
            <button
              onClick={() => {
                setDraftPages(draftItems);
                setDraftCursor(drafts.data.nextCursor ?? '');
              }}
            >
              {t('Load more drafts')}
            </button>
          )}
        </div>
      )}
      <LibraryResults count={(data) => data.items.length} load={data} retry={data.reload}>
        {(result) => (
          <>
            {result.items.length ? (
              <LibraryGrid>
                {result.items.map((set) => (
                  <SetCard key={set.id} set={set} />
                ))}
              </LibraryGrid>
            ) : (
              <LibraryEmpty
                art="wood"
                action={
                  filtered ? (
                    <button onClick={reset}>{t('Clear filters')}</button>
                  ) : (
                    <Link className="btn primary" to="/sets/new">
                      {t('Create a set')}
                    </Link>
                  )
                }
              >
                {filtered
                  ? t('No results match these filters.')
                  : t('No sets found. Try another search or create the first one.')}
              </LibraryEmpty>
            )}
            {result.nextCursor && (
              <button onClick={() => setCursor(result.nextCursor ?? '')}>{t('Next page')}</button>
            )}
            {cursor && <button onClick={() => setCursor('')}>{t('First page')}</button>}
          </>
        )}
      </LibraryResults>
    </section>
  );
}
function SetCard({ set: s }: { set: SetInfo }) {
  useLocale();
  const latest = s.versions[0];
  return (
    <LibraryCard>
      <Link to={'/sets/' + s.id}>
        {latest ? (
          <img
            className="library-preview"
            src={`/api/v1/sets/${s.id}/versions/${latest.id}/preview`}
            alt=""
            loading="lazy"
          />
        ) : (
          <div className="library-preview library-preview-placeholder">
            <GameArt name="wood" size={72} />
          </div>
        )}
        <h2>{s.title}</h2>
      </Link>
      <p className="library-metadata">
        <RichMessage
          source={'By {slot0} · {slot1} terrains · {slot2} resources'}
          slots={{
            slot0: s.owner.displayName,
            slot1: latest?.terrainCount ?? 0,
            slot2: latest?.resourceCount ?? 0,
          }}
          singular={'By {slot0} · {slot1} terrain · {slot2} resources'}
          count={Number(latest?.terrainCount ?? 0)}
        />
      </p>
      <p className="library-description">{s.description}</p>
      <small>
        <RichMessage
          source={'{slot0} likes · {slot1} downloads · {slot2}'}
          slots={{ slot0: s.likes, slot1: s.downloads, slot2: latest?.license }}
        />
      </small>
    </LibraryCard>
  );
}
export function SetDetail({ id }: { id: string }) {
  useLocale();
  const data = useLoad((signal) => request<SetInfo>('GET', '/api/v1/sets/' + id, { signal }), [id]);
  const { account } = useSession(),
    { navigate } = useRouter();
  const [error, setError] = useState(''),
    [busy, setBusy] = useState(false),
    [report, setReport] = useState(false),
    [contents, setContents] = useState<SetPackage | null>(null);
  async function action(fn: () => Promise<unknown>) {
    setBusy(true);
    setError('');
    try {
      await fn();
      data.reload();
    } catch (e) {
      setError(String(e instanceof Error ? e.message : e));
    } finally {
      setBusy(false);
    }
  }
  if (data.status === 'loading') return <p role="status">{t('Loading set…')}</p>;
  if (data.status === 'error') return <p role="alert">{translateError(data.error)}</p>;
  const s = data.data,
    latest = s.versions[0],
    own = account?.id === s.owner.id;
  return (
    <section className="set-detail">
      <Link to="/sets">{t('← Set library')}</Link>
      <h1>{s.title}</h1>
      <p>
        <RichMessage source={'By {slot0}'} slots={{ slot0: s.owner.displayName }} />
      </p>
      <p>{s.description}</p>
      {s.hidden && (
        <p role="alert">
          <RichMessage source={'This set is hidden. {slot0}'} slots={{ slot0: s.hiddenReason }} />
        </p>
      )}
      {error && <p role="alert">{displayMessage(error)}</p>}
      {latest && !s.hidden && (
        <img
          className="set-contact"
          src={`/api/v1/sets/${id}/versions/${latest.id}/preview`}
          alt={t('{value0}: terrain and resource previews', { value0: s.title })}
        />
      )}
      <div className="set-actions">
        {latest && !s.hidden && (
          <>
            <Link className="button" to={`/terrain-studio?version=${latest.id}`}>
              {t('Remix with AI')}
            </Link>
            <a className="button" href={`/api/v1/sets/${id}/versions/${latest.id}/file`}>
              {t('Download set')}
            </a>
          </>
        )}
        {account && (
          <button
            disabled={busy || s.hidden}
            onClick={() =>
              void action(() => request(s.liked ? 'DELETE' : 'PUT', `/api/v1/sets/${id}/like`))
            }
          >
            <RichMessage
              source={'{slot0} · {slot1}'}
              slots={{ slot0: s.liked ? t('Unlike') : t('Like'), slot1: s.likes }}
            />
          </button>
        )}
        {account && !own && <button onClick={() => setReport(!report)}>{t('Report')}</button>}
        {own && latest && (
          <button
            disabled={busy}
            onClick={() =>
              void action(async () => {
                const pack = await request<SetDraft['package']>(
                  'GET',
                  `/api/v1/sets/${id}/versions/${latest.id}/file`,
                );
                const draft = await request<SetDraft>('POST', '/api/v1/set-drafts', {
                  body: newRelease(pack),
                });
                navigate('/sets/drafts/' + draft.id);
              })
            }
          >
            {t('Create new release')}
          </button>
        )}
        {own && (
          <button
            disabled={busy}
            onClick={() =>
              void action(() =>
                request('PATCH', '/api/v1/sets/' + id, {
                  body: {
                    title: s.title,
                    description: s.description,
                    tags: s.tags,
                    visibility: s.visibility === 'private' ? 'public' : 'private',
                  },
                }),
              )
            }
          >
            {s.visibility === 'private' ? t('Make public') : t('Withdraw from library')}
          </button>
        )}
        {isModerator(account) && (
          <button
            disabled={busy}
            onClick={() =>
              void action(() =>
                request('POST', `/api/v1/admin/sets/${id}/${s.hidden ? 'unhide' : 'hide'}`, {
                  body: s.hidden ? {} : { reason: 'Hidden following moderator review' },
                }),
              )
            }
          >
            {s.hidden ? t('Unhide') : t('Hide set')}
          </button>
        )}
      </div>
      {report && (
        <form
          onSubmit={(e) => {
            e.preventDefault();
            const fields = new FormData(e.currentTarget);
            void action(async () => {
              await request('POST', `/api/v1/sets/${id}/reports`, {
                body: { reason: fields.get('reason'), details: fields.get('details') },
              });
              setReport(false);
            });
          }}
        >
          <label>
            {t('Reason')}
            <select name="reason">
              <option value="copyright">{t('Copyright')}</option>
              <option value="offensive">{t('Inappropriate content')}</option>
              <option value="other">{t('Other')}</option>
            </select>
          </label>
          <label>
            {t('Details')}
            <textarea name="details" maxLength={2000} required />
          </label>
          <button disabled={busy}>{t('Send report')}</button>
        </form>
      )}
      {latest && !s.hidden && (
        <section aria-label={t('Set contents')}>
          <h2>{t('Contents & gameplay properties')}</h2>
          {!contents ? (
            <button
              disabled={busy}
              onClick={() =>
                void action(async () => {
                  setContents(
                    await request<SetPackage>(
                      'GET',
                      `/api/v1/sets/${id}/versions/${latest.id}/file`,
                    ),
                  );
                })
              }
            >
              {t('Inspect latest release')}
            </button>
          ) : (
            [
              ...contents.terrains.map((entry) => ({ entry, kind: t('Terrain') })),
              ...contents.resources.map((entry) => ({ entry, kind: t('Resource') })),
            ].map(({ entry, kind }) => {
              const presentation = entry['presentation'] as Record<string, unknown> | undefined;
              return (
                <details key={String(entry['key'])}>
                  <summary>
                    <RichMessage
                      source={'{slot0} · {slot1}'}
                      slots={{
                        slot0: kind,
                        slot1: String(entry['name'] ?? presentation?.['name'] ?? entry['key']),
                      }}
                    />
                  </summary>
                  {typeof entry['base'] === 'string' && (
                    <p>
                      <RichMessage
                        source={'Gameplay preset: {slot0}'}
                        slots={{ slot0: entry['base'] }}
                      />
                    </p>
                  )}
                  <dl>
                    {Object.entries((entry['properties'] ?? {}) as Record<string, unknown>).map(
                      ([key, value]) => (
                        <div key={key}>
                          <dt>{key.replace(/([A-Z])/g, ' $1')}</dt>
                          <dd>
                            {typeof value === 'object' ? JSON.stringify(value) : String(value)}
                          </dd>
                        </div>
                      ),
                    )}
                  </dl>
                  <details>
                    <summary>{t('Full definition and frame mappings')}</summary>
                    <pre className="set-definition">
                      {JSON.stringify(
                        kind === 'Terrain'
                          ? {
                              ...entry,
                              customArtwork: contents.assets.terrains[String(entry['key'])] ?? null,
                            }
                          : entry,
                        null,
                        2,
                      )}
                    </pre>
                  </details>
                </details>
              );
            })
          )}
        </section>
      )}
      <h2>{t('Use in a map')}</h2>
      <p>
        {t(
          'Open the map editor’s Set Library to import a release, or import this downloaded package. Only custom content is included in shared maps. Each map keeps its own editable copy.',
        )}
      </p>
      <h2>{t('Releases & credits')}</h2>
      {s.versions.map((v) => (
        <article key={v.id}>
          <h3>{v.label}</h3>
          <p>{v.notes}</p>
          <p>
            <RichMessage
              source={'{slot0} terrains · {slot1} resources · {slot2}'}
              slots={{ slot0: v.terrainCount, slot1: v.resourceCount, slot2: v.license }}
              singular={'{slot0} terrain · {slot1} resources · {slot2}'}
              count={Number(v.terrainCount)}
            />
          </p>
          <ul>
            {v.credits.map((c, i) => (
              <li key={i}>
                <RichMessage
                  source={'{slot0} · {slot1}{slot2}'}
                  slots={{
                    slot0: c.author,
                    slot1: c.license,
                    slot2: c.source && (
                      <span>
                        {' '}
                        {t(' · ')}
                        {c.source}
                      </span>
                    ),
                  }}
                />
              </li>
            ))}
          </ul>
          {!s.hidden && (
            <a href={`/api/v1/sets/${id}/versions/${v.id}/file`}>{t('Download this release')}</a>
          )}
        </article>
      ))}
    </section>
  );
}

export function SetReports() {
  useLocale();
  const { account } = useSession();
  const data = useLoad(
    (signal) =>
      request<{
        items: {
          id: string;
          set_id: string;
          reason: string;
          details: string;
          created_at: string;
        }[];
      }>('GET', '/api/v1/admin/sets/reports', { signal }),
    [],
  );
  const [error, setError] = useState(''),
    [busy, setBusy] = useState(false);
  if (!isModerator(account)) return <p>{t('Moderator access is required.')}</p>;
  async function resolve(id: string, status: string, hide: boolean) {
    setBusy(true);
    setError('');
    try {
      await request('POST', `/api/v1/admin/sets/reports/${id}/resolve`, {
        body: {
          status,
          hideMap: hide,
          hideReason: hide ? t('Hidden following moderator review') : undefined,
        },
      });
      data.reload();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <section>
      <h1>{t('Set reports')}</h1>
      {error && <p role="alert">{displayMessage(error)}</p>}
      {data.status === 'loading' ? (
        <p>{t('Loading reports…')}</p>
      ) : data.status === 'error' ? (
        <p role="alert">{translateError(data.error)}</p>
      ) : data.data.items.length === 0 ? (
        <p>{t('No open reports.')}</p>
      ) : (
        data.data.items.map((r) => (
          <article key={r.id}>
            <h2>
              <Link to={'/sets/' + r.set_id}>{t('Review set')}</Link>
            </h2>
            <p>
              <RichMessage
                source={'{slot0} · {slot1}'}
                slots={{ slot0: r.reason, slot1: r.created_at }}
              />
            </p>
            <p>{r.details}</p>
            <button disabled={busy} onClick={() => void resolve(r.id, 'resolved', true)}>
              {t('Hide set and resolve')}
            </button>
            <button disabled={busy} onClick={() => void resolve(r.id, 'resolved', false)}>
              {t('Resolve')}
            </button>
            <button disabled={busy} onClick={() => void resolve(r.id, 'dismissed', false)}>
              {t('Dismiss')}
            </button>
          </article>
        ))
      )}
    </section>
  );
}
