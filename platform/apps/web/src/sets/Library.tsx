import { useState } from 'react';
import type { SetInfo, SetList, SetDraft, SetDraftSummary, SetPackage } from '@glob2/protocol';
import { request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession, isModerator } from '../state.tsx';
import { newRelease } from './model.ts';
import './sets.css';

export function SetLibrary({ mine = false }: { mine?: boolean }) {
  const { account } = useSession();
  const [q, setQ] = useState(''),
    [sort, setSort] = useState('newest'),
    [kind, setKind] = useState(''),
    [license, setLicense] = useState('');
  const [cursor, setCursor] = useState('');
  const [draftCursor, setDraftCursor] = useState(''),
    [draftPages, setDraftPages] = useState<SetDraftSummary[]>([]);
  const data = useLoad(
    (signal) =>
      request<SetList>('GET', '/api/v1/sets', {
        query: { q, sort, kind, license, owner: mine ? 'me' : undefined, cursor },
        signal,
      }),
    [q, sort, kind, license, mine, cursor],
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
  return (
    <section className="set-library">
      <header className="set-heading">
        <div>
          <h1>{mine ? 'My sets' : 'Terrain & resource sets'}</h1>
          <p>Build a consistent world with custom terrain, resources, and artwork.</p>
        </div>
        <div>
          <Link to={mine ? '/sets' : '/sets/mine'}>{mine ? 'Browse library' : 'My sets'}</Link>
          <Link className="button" to="/sets/new">
            Create a set
          </Link>
          <Link className="button" to="/terrain-studio">
            Create with AI
          </Link>
        </div>
      </header>
      {isModerator(account) && <Link to="/sets/reports">Moderation reports</Link>}
      <div className="set-filters">
        <label>
          Search
          <input
            value={q}
            onChange={(e) => {
              setQ(e.target.value);
              setCursor('');
            }}
            placeholder="Theme, title or creator"
          />
        </label>
        <label>
          Contents
          <select
            value={kind}
            onChange={(e) => {
              setKind(e.target.value);
              setCursor('');
            }}
          >
            <option value="">All sets</option>
            <option value="terrain">Terrain</option>
            <option value="resource">Resources</option>
            <option value="both">Both</option>
          </select>
        </label>
        <label>
          License
          <select
            value={license}
            onChange={(e) => {
              setLicense(e.target.value);
              setCursor('');
            }}
          >
            <option value="">All licenses</option>
            <option value="CC0-1.0">CC0</option>
            <option value="CC-BY-4.0">CC BY</option>
          </select>
        </label>
        <label>
          Sort
          <select
            value={sort}
            onChange={(e) => {
              setSort(e.target.value);
              setCursor('');
            }}
          >
            {['newest', 'updated', 'likes', 'downloads'].map((x) => (
              <option key={x}>{x}</option>
            ))}
          </select>
        </label>
      </div>
      {mine && (
        <div className="set-drafts" aria-label="Your drafts">
          <h2>Drafts</h2>
          {draftItems.map((d) => (
            <Link key={d.id} to={'/sets/drafts/' + d.id}>
              {d.title} · Continue editing
            </Link>
          ))}
          {drafts.status === 'loading' && <p role="status">Loading drafts…</p>}
          {drafts.status === 'error' && <p role="alert">{drafts.error.message}</p>}
          {drafts.status === 'ready' && !draftItems.length && <p>No unpublished drafts.</p>}
          {drafts.status === 'ready' && drafts.data.nextCursor && (
            <button
              onClick={() => {
                setDraftPages(draftItems);
                setDraftCursor(drafts.data.nextCursor ?? '');
              }}
            >
              Load more drafts
            </button>
          )}
        </div>
      )}
      {data.status === 'loading' ? (
        <p role="status">Loading sets…</p>
      ) : data.status === 'error' ? (
        <p role="alert">{data.error.message}</p>
      ) : (
        <>
          <div className="set-cards">
            {data.data.items.map((s) => (
              <SetCard key={s.id} set={s} />
            ))}
          </div>
          {!data.data.items.length && (
            <p>No sets found. Try another search or create the first one.</p>
          )}
          {data.data.nextCursor && (
            <button onClick={() => setCursor(data.data.nextCursor ?? '')}>Next page</button>
          )}
          {cursor && <button onClick={() => setCursor('')}>First page</button>}
        </>
      )}
    </section>
  );
}
function SetCard({ set: s }: { set: SetInfo }) {
  const latest = s.versions[0];
  return (
    <article className="set-card">
      <Link to={'/sets/' + s.id}>
        {latest && (
          <img src={`/api/v1/sets/${s.id}/versions/${latest.id}/preview`} alt="" loading="lazy" />
        )}
        <h2>{s.title}</h2>
      </Link>
      <p>{s.description}</p>
      <p>
        By {s.owner.displayName} · {latest?.terrainCount ?? 0} terrains ·{' '}
        {latest?.resourceCount ?? 0} resources
      </p>
      <small>
        {s.likes} likes · {s.downloads} downloads · {latest?.license}
      </small>
    </article>
  );
}
export function SetDetail({ id }: { id: string }) {
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
  if (data.status === 'loading') return <p role="status">Loading set…</p>;
  if (data.status === 'error') return <p role="alert">{data.error.message}</p>;
  const s = data.data,
    latest = s.versions[0],
    own = account?.id === s.owner.id;
  return (
    <section className="set-detail">
      <Link to="/sets">← Set library</Link>
      <h1>{s.title}</h1>
      <p>By {s.owner.displayName}</p>
      <p>{s.description}</p>
      {s.hidden && <p role="alert">This set is hidden. {s.hiddenReason}</p>}
      {error && <p role="alert">{error}</p>}
      {latest && !s.hidden && (
        <img
          className="set-contact"
          src={`/api/v1/sets/${id}/versions/${latest.id}/preview`}
          alt={`${s.title}: terrain and resource previews`}
        />
      )}
      <div className="set-actions">
        {latest && !s.hidden && (
          <>
            <Link className="button" to={`/terrain-studio?version=${latest.id}`}>
              Remix with AI
            </Link>
            <a className="button" href={`/api/v1/sets/${id}/versions/${latest.id}/file`}>
              Download set
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
            {s.liked ? 'Unlike' : 'Like'} · {s.likes}
          </button>
        )}
        {account && !own && <button onClick={() => setReport(!report)}>Report</button>}
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
            Create new release
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
            {s.visibility === 'private' ? 'Make public' : 'Withdraw from library'}
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
            {s.hidden ? 'Unhide' : 'Hide set'}
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
            Reason
            <select name="reason">
              <option value="copyright">Copyright</option>
              <option value="offensive">Inappropriate content</option>
              <option value="other">Other</option>
            </select>
          </label>
          <label>
            Details
            <textarea name="details" maxLength={2000} required />
          </label>
          <button disabled={busy}>Send report</button>
        </form>
      )}
      {latest && !s.hidden && (
        <section aria-label="Set contents">
          <h2>Contents & gameplay properties</h2>
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
              Inspect latest release
            </button>
          ) : (
            [
              ...contents.terrains.map((entry) => ({ entry, kind: 'Terrain' })),
              ...contents.resources.map((entry) => ({ entry, kind: 'Resource' })),
            ].map(({ entry, kind }) => {
              const presentation = entry['presentation'] as Record<string, unknown> | undefined;
              return (
                <details key={String(entry['key'])}>
                  <summary>
                    {kind} · {String(entry['name'] ?? presentation?.['name'] ?? entry['key'])}
                  </summary>
                  {typeof entry['base'] === 'string' && <p>Gameplay preset: {entry['base']}</p>}
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
                    <summary>Full definition and frame mappings</summary>
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
      <h2>Use in a map</h2>
      <p>
        Open the map editor’s Set Library to import a release, or import this downloaded package.
        Only custom content is included in shared maps. Each map keeps its own editable copy.
      </p>
      <h2>Releases & credits</h2>
      {s.versions.map((v) => (
        <article key={v.id}>
          <h3>{v.label}</h3>
          <p>{v.notes}</p>
          <p>
            {v.terrainCount} terrains · {v.resourceCount} resources · {v.license}
          </p>
          <ul>
            {v.credits.map((c, i) => (
              <li key={i}>
                {c.author} · {c.license}
                {c.source && <span> · {c.source}</span>}
              </li>
            ))}
          </ul>
          {!s.hidden && (
            <a href={`/api/v1/sets/${id}/versions/${v.id}/file`}>Download this release</a>
          )}
        </article>
      ))}
    </section>
  );
}

export function SetReports() {
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
  if (!isModerator(account)) return <p>Moderator access is required.</p>;
  async function resolve(id: string, status: string, hide: boolean) {
    setBusy(true);
    setError('');
    try {
      await request('POST', `/api/v1/admin/sets/reports/${id}/resolve`, {
        body: {
          status,
          hideMap: hide,
          hideReason: hide ? 'Hidden following moderator review' : undefined,
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
      <h1>Set reports</h1>
      {error && <p role="alert">{error}</p>}
      {data.status === 'loading' ? (
        <p>Loading reports…</p>
      ) : data.status === 'error' ? (
        <p role="alert">{data.error.message}</p>
      ) : data.data.items.length === 0 ? (
        <p>No open reports.</p>
      ) : (
        data.data.items.map((r) => (
          <article key={r.id}>
            <h2>
              <Link to={'/sets/' + r.set_id}>Review set</Link>
            </h2>
            <p>
              {r.reason} · {r.created_at}
            </p>
            <p>{r.details}</p>
            <button disabled={busy} onClick={() => void resolve(r.id, 'resolved', true)}>
              Hide set and resolve
            </button>
            <button disabled={busy} onClick={() => void resolve(r.id, 'resolved', false)}>
              Resolve
            </button>
            <button disabled={busy} onClick={() => void resolve(r.id, 'dismissed', false)}>
              Dismiss
            </button>
          </article>
        ))
      )}
    </section>
  );
}
