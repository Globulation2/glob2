import { useState } from 'react';
import type { BuildingFamily, BuildingLibrary as Library } from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded, ErrorNotice } from '../components/common.tsx';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import '../styles/buildings.css';
/** A hidden or rejected family should never be advertised as still validating. */
function availability(family: BuildingFamily): string {
  if (family.hidden) return 'Hidden by a moderator';
  if (family.releases.some((release) => release.status === 'valid')) return 'Ready to download';
  if (family.releases.some((release) => release.status === 'pending')) return 'Awaiting validation';
  if (family.releases.some((release) => release.status === 'error'))
    return 'Validation failed; author action needed';
  return 'No validated release';
}
export function BuildingLibrary({ id }: { id?: string }) {
  const { account } = useSession(),
    { navigate } = useRouter();
  const [search, setSearch] = useState(''),
    [query, setQuery] = useState(''),
    [filter, setFilter] = useState('all'),
    [sort, setSort] = useState('updated'),
    [revision, setRevision] = useState(0),
    [error, setError] = useState<Error>(),
    [busy, setBusy] = useState(false),
    [cursor, setCursor] = useState<string>(),
    [message, setMessage] = useState(''),
    [manageOpen, setManageOpen] = useState(false);
  const list = useLoad(
    (signal) =>
      request<Library>('GET', '/api/v1/buildings', {
        signal,
        query: {
          q: query,
          sort,
          cursor,
          owner: filter === 'mine' ? 'me' : undefined,
          favourites: filter === 'favourites' ? 'true' : undefined,
        },
      }),
    [query, filter, sort, cursor, revision, account?.id],
  );
  const detail = useLoad(
    (signal) =>
      id
        ? request<BuildingFamily>('GET', '/api/v1/buildings/' + id, { signal })
        : Promise.resolve(undefined),
    [id, revision, account?.id],
  );
  async function action(operation: () => Promise<unknown>) {
    setBusy(true);
    setError(undefined);
    try {
      await operation();
      setMessage('Saved.');
      setRevision((v) => v + 1);
    } catch (e) {
      setError(e instanceof Error ? e : new Error(String(e)));
    } finally {
      setBusy(false);
    }
  }
  return (
    <div className="building-studio">
      <h1>{id ? 'Building family' : 'Building library'}</h1>
      <p>
        <Link to="/ai-building-studio">Create with AI</Link>
        <Link to="/building-studio">Create a building family</Link>
        {id && (
          <>
            {' '}
            · <Link to="/buildings">Browse the library</Link>
          </>
        )}
      </p>
      <details className="building-help">
        <summary>Use buildings in the game</summary>
        <p>
          Open Building families when creating a new game or a new map in the editor, install a
          compatible release, and enable it. Enabled families apply to future generated maps
          alongside stock buildings. Existing maps and saves keep their own buildings.
        </p>
        <p>
          For an unlisted family, copy its page link and paste it into Family link or ID in the
          game’s Building families picker, then choose Open family. Use the same online instance
          where the family was published. Private families also require signing in as their owner.
        </p>
        <p>
          To play online, publish the generated map in the map library and select it for your room.
          Download family exports a ZIP for importing or editing in Building Studio.
        </p>
      </details>
      {error && <ErrorNotice error={error} />}
      {message && <p role="status">{message}</p>}
      {id ? (
        <Loaded load={detail}>
          {(family) =>
            family ? (
              <>
                <h2>{family.name}</h2>
                {!family.hidden &&
                  family.releases.find((v) => v.status === 'valid' && v.artworkHash) && (
                    <img
                      className="building-thumbnail"
                      src={`/api/v1/buildings/${family.id}/releases/${family.releases.find((v) => v.status === 'valid' && v.artworkHash)?.id}/thumbnail`}
                      alt={`${family.name} artwork`}
                      width={160}
                      height={160}
                    />
                  )}
                <p>By {family.owner.displayName}</p>
                <label>
                  Family link
                  <input
                    readOnly
                    value={window.location.origin + '/buildings/' + family.id}
                    onFocus={(event) => event.currentTarget.select()}
                    onClick={(event) => event.currentTarget.select()}
                  />
                </label>
                <p>
                  Copy this link into Family link or ID in the game’s Building families picker to
                  open this family directly.
                </p>
                <p>{family.description}</p>
                {family.hidden && <p role="status">This family has been hidden by a moderator.</p>}
                <p>
                  {family.likes} likes · {family.downloads} downloads
                </p>
                {account && (
                  <div className="building-actions">
                    <button
                      type="button"
                      disabled={busy}
                      onClick={() =>
                        void action(() =>
                          request(
                            family.liked ? 'DELETE' : 'PUT',
                            `/api/v1/buildings/${family.id}/like`,
                          ),
                        )
                      }
                    >
                      {family.liked ? 'Unlike' : 'Like'}
                    </button>
                    <button
                      type="button"
                      disabled={busy}
                      onClick={() =>
                        void action(() =>
                          request(
                            family.favourite ? 'DELETE' : 'PUT',
                            `/api/v1/buildings/${family.id}/favourite`,
                          ),
                        )
                      }
                    >
                      {family.favourite ? 'Remove favourite' : 'Favourite'}
                    </button>
                  </div>
                )}
                {account && (account.role === 'moderator' || account.role === 'admin') && (
                  <form
                    onSubmit={(e) => {
                      e.preventDefault();
                      const data = new FormData(e.currentTarget);
                      void action(() =>
                        request('PUT', `/api/v1/buildings/${family.id}/moderation`, {
                          body: { hidden: !family.hidden, reason: data.get('reason') },
                        }),
                      );
                    }}
                  >
                    <label>
                      Moderation reason
                      <input name="reason" required={!family.hidden} maxLength={2000} />
                    </label>
                    <button disabled={busy}>
                      {family.hidden ? 'Restore family' : 'Hide family'}
                    </button>
                  </form>
                )}
                {account?.id === family.owner.id && (
                  <details
                    open={manageOpen}
                    onToggle={(event) => setManageOpen(event.currentTarget.open)}
                  >
                    <summary>Manage published family</summary>
                    <form
                      key={family.updatedAt}
                      onSubmit={(event) => {
                        event.preventDefault();
                        const data = new FormData(event.currentTarget);
                        void action(() =>
                          request('PATCH', `/api/v1/buildings/${family.id}`, {
                            body: {
                              name: data.get('name'),
                              description: data.get('description'),
                              visibility: data.get('visibility'),
                            },
                          }),
                        );
                      }}
                    >
                      <label>
                        Published family name
                        <input name="name" required maxLength={128} defaultValue={family.name} />
                      </label>
                      <label>
                        Published description
                        <textarea
                          name="description"
                          maxLength={4000}
                          defaultValue={family.description}
                        />
                      </label>
                      <label>
                        Published visibility
                        <select name="visibility" defaultValue={family.visibility}>
                          <option value="public">Public library</option>
                          <option value="unlisted">Unlisted (people with the link)</option>
                          <option value="private">Private</option>
                        </select>
                      </label>
                      <button disabled={busy}>Save published details</button>
                    </form>
                    <p>
                      These details apply to every release. Changing visibility also changes who can
                      download them. Edit building definitions in your Studio draft and publish a
                      new release.
                    </p>
                    <button
                      type="button"
                      disabled={busy}
                      onClick={() => {
                        if (
                          !window.confirm(
                            `Withdraw “${family.name}” and all its releases? Library downloads and links will stop working. Studio drafts and copies embedded in existing maps or saves remain available.`,
                          )
                        )
                          return;
                        void action(async () => {
                          await request('DELETE', `/api/v1/buildings/${family.id}`);
                          navigate('/buildings');
                        });
                      }}
                    >
                      Withdraw published family
                    </button>
                  </details>
                )}
                <h2>Releases</h2>
                <p>
                  Choose a validated release for the simulation version you play. Each download
                  includes its artwork.
                </p>
                <button type="button" onClick={() => setRevision((v) => v + 1)}>
                  Refresh validation
                </button>
                <ul>
                  {family.releases.map((v) => (
                    <li key={v.id}>
                      <p>
                        <strong>{new Date(v.createdAt).toLocaleString()}</strong> · {v.status}
                      </p>
                      <details>
                        <summary>
                          Simulation {v.simVersion.split('-').slice(0, 2).join('-')}
                        </summary>
                        <p>
                          Full compatibility key: <code>{v.simVersion}</code>
                        </p>
                      </details>
                      {v.error && <p role="status">{v.error}</p>}
                      {v.status === 'valid' && !family.hidden && (
                        <div className="building-actions">
                          <a
                            className="button"
                            href={`/api/v1/buildings/${family.id}/releases/${v.id}/archive`}
                          >
                            Download family
                          </a>
                          {account && (
                            <button
                              type="button"
                              disabled={busy}
                              onClick={() =>
                                void action(async () => {
                                  const draft = await request<{ id: string }>(
                                    'POST',
                                    `/api/v1/buildings/${family.id}/releases/${v.id}/fork`,
                                    { body: {} },
                                  );
                                  navigate('/building-studio/' + draft.id);
                                })
                              }
                            >
                              Fork into Studio
                            </button>
                          )}
                        </div>
                      )}
                    </li>
                  ))}
                </ul>
                {account && (
                  <form
                    onSubmit={(e) => {
                      e.preventDefault();
                      const data = new FormData(e.currentTarget);
                      void action(() =>
                        request('POST', `/api/v1/buildings/${family.id}/reports`, {
                          body: { reason: data.get('reason') },
                        }),
                      );
                    }}
                  >
                    <label>
                      Report a problem
                      <textarea name="reason" required maxLength={2000} />
                    </label>
                    <button disabled={busy}>Send report</button>
                  </form>
                )}
              </>
            ) : null
          }
        </Loaded>
      ) : (
        <>
          <form
            onSubmit={(e) => {
              e.preventDefault();
              setCursor(undefined);
              setQuery(search);
            }}
          >
            <label>
              Search buildings
              <input value={search} maxLength={128} onChange={(e) => setSearch(e.target.value)} />
            </label>
            <button>Search</button>
          </form>
          {account && (
            <label>
              Show
              <select
                value={filter}
                onChange={(e) => (setCursor(undefined), setFilter(e.target.value))}
              >
                <option value="all">Public library</option>
                <option value="mine">My families</option>
                <option value="favourites">My favourites</option>
              </select>
            </label>
          )}
          <label>
            Sort
            <select
              value={sort}
              onChange={(e) => {
                setCursor(undefined);
                setSort(e.target.value);
              }}
            >
              <option value="updated">Recently updated</option>
              <option value="newest">Newest</option>
              <option value="likes">Most liked</option>
              <option value="downloads">Most downloaded</option>
            </select>
          </label>
          <Loaded load={list}>
            {(data) => (
              <>
                <ul className="building-family-list">
                  {data.items.map((f) => (
                    <li key={f.id}>
                      <h2>
                        <Link to={'/buildings/' + f.id}>{f.name}</Link>
                      </h2>
                      {!f.hidden &&
                        f.releases.find(
                          (release) => release.status === 'valid' && release.artworkHash,
                        ) && (
                          <img
                            className="building-thumbnail"
                            src={`/api/v1/buildings/${f.id}/releases/${f.releases.find((release) => release.status === 'valid' && release.artworkHash)?.id}/thumbnail`}
                            alt={`${f.name} artwork`}
                            width={96}
                            height={96}
                          />
                        )}
                      <p>{f.description}</p>
                      <p>
                        By {f.owner.displayName} · {f.likes} likes · {availability(f)}
                      </p>
                    </li>
                  ))}
                </ul>
                {data.items.length === 0 && <p>No families found.</p>}
                {data.nextCursor && (
                  <button type="button" onClick={() => setCursor(data.nextCursor)}>
                    Next page
                  </button>
                )}
                {cursor && (
                  <button type="button" onClick={() => setCursor(undefined)}>
                    First page
                  </button>
                )}
              </>
            )}
          </Loaded>
        </>
      )}
    </div>
  );
}
