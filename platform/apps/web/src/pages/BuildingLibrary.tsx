import { statusLabel } from '../i18n.tsx';
import { getLocale } from '../i18n.tsx';
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
import type { BuildingFamily, BuildingLibrary as Library } from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded, ErrorNotice } from '../components/common.tsx';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import '../styles/buildings.css';
/** A hidden or rejected family should never be advertised as still validating. */
function availability(family: BuildingFamily): string {
  if (family.hidden) return t('Hidden by a moderator');
  if (family.releases.some((release) => release.status === 'valid')) return t('Ready to download');
  if (family.releases.some((release) => release.status === 'pending'))
    return t('Awaiting validation');
  if (family.releases.some((release) => release.status === 'error'))
    return 'Validation failed; author action needed';
  return t('No validated release');
}
export function BuildingLibrary({ id }: { id?: string }) {
  useLocale();
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
  const applySearch = useLibrarySearch(search, query, (value) => {
    setQuery(value);
    setCursor(undefined);
  });
  const reset = () => {
    setSearch('');
    setQuery('');
    setSort('updated');
    setCursor(undefined);
  };
  return (
    <div className={id ? 'building-studio' : 'library-page'}>
      {!id && (
        <>
          <LibraryHeader
            art="inn"
            title={t('Building library')}
            description={t('Create a building or an upgrade family with custom artwork.')}
            actions={
              <>
                <Link className="btn primary" to="/building-studio">
                  {t('Create a building family')}
                </Link>
                <Link className="btn" to="/ai-building-studio">
                  {t('Create with AI')}
                </Link>
              </>
            }
          />
          {account && (
            <LibraryNav label={t('Building library')}>
              {(
                [
                  ['all', t('Browse')],
                  ['favourites', t('My favourites')],
                  ['mine', t('My families')],
                ] as const
              ).map(([value, label]) => (
                <button
                  key={value}
                  className={filter === value ? 'on' : ''}
                  aria-pressed={filter === value}
                  onClick={() => {
                    setFilter(value);
                    setCursor(undefined);
                  }}
                >
                  {label}
                </button>
              ))}
            </LibraryNav>
          )}
        </>
      )}
      {id && (
        <>
          <h1>{id ? t('Building family') : t('Building library')}</h1>
          <p>
            <Link to="/ai-building-studio">{t('Create with AI')}</Link>
            <Link to="/building-studio">{t('Create a building family')}</Link>
            {id && (
              <>
                {' '}
                {t(' · ')}
                <Link to="/buildings">{t('Browse the library')}</Link>
              </>
            )}
          </p>
        </>
      )}
      <details className={id ? 'building-help' : 'library-help'}>
        <summary>{t('Use buildings in the game')}</summary>
        <p>
          {t(
            'Open Building families when creating a new game or a new map in the editor, install a compatible release, and enable it. Enabled families apply to future generated maps alongside stock buildings. Existing maps and saves keep their own buildings.',
          )}
        </p>
        <p>
          {t(
            'For an unlisted family, copy its page link and paste it into Family link or ID in the game’s Building families picker, then choose Open family. Use the same online instance where the family was published. Private families also require signing in as their owner.',
          )}
        </p>
        <p>
          {t(
            'To play online, publish the generated map in the map library and select it for your room. Download family exports a ZIP for importing or editing in Building Studio.',
          )}
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
                      alt={t('{value0} artwork', { value0: family.name })}
                      width={160}
                      height={160}
                    />
                  )}
                <p>
                  <RichMessage source={'By {slot0}'} slots={{ slot0: family.owner.displayName }} />
                </p>
                <label>
                  {t('Family link')}
                  <input
                    readOnly
                    value={window.location.origin + '/buildings/' + family.id}
                    onFocus={(event) => event.currentTarget.select()}
                    onClick={(event) => event.currentTarget.select()}
                  />
                </label>
                <p>
                  {t(
                    'Copy this link into Family link or ID in the game’s Building families picker to open this family directly.',
                  )}
                </p>
                <p>{family.description}</p>
                {family.hidden && (
                  <p role="status">{t('This family has been hidden by a moderator.')}</p>
                )}
                <p>
                  <RichMessage
                    source={'{slot0} likes · {slot1} downloads'}
                    slots={{ slot0: family.likes, slot1: family.downloads }}
                  />
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
                      {family.liked ? t('Unlike') : t('Like')}
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
                      {family.favourite ? t('Remove favourite') : t('Favourite')}
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
                      {t('Moderation reason')}
                      <input name="reason" required={!family.hidden} maxLength={2000} />
                    </label>
                    <button disabled={busy}>
                      {family.hidden ? t('Restore family') : t('Hide family')}
                    </button>
                  </form>
                )}
                {account?.id === family.owner.id && (
                  <details
                    open={manageOpen}
                    onToggle={(event) => setManageOpen(event.currentTarget.open)}
                  >
                    <summary>{t('Manage published family')}</summary>
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
                        {t('Published family name')}
                        <input name="name" required maxLength={128} defaultValue={family.name} />
                      </label>
                      <label>
                        {t('Published description')}
                        <textarea
                          name="description"
                          maxLength={4000}
                          defaultValue={family.description}
                        />
                      </label>
                      <label>
                        {t('Published visibility')}
                        <select name="visibility" defaultValue={family.visibility}>
                          <option value="public">{t('Public library')}</option>
                          <option value="unlisted">{t('Unlisted (people with the link)')}</option>
                          <option value="private">{t('Private')}</option>
                        </select>
                      </label>
                      <button disabled={busy}>{t('Save published details')}</button>
                    </form>
                    <p>
                      {t(
                        'These details apply to every release. Changing visibility also changes who can download them. Edit building definitions in your Studio draft and publish a new release.',
                      )}
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
                      {t('Withdraw published family')}
                    </button>
                  </details>
                )}
                <h2>{t('Releases')}</h2>
                <p>
                  {t(
                    'Choose a validated release for the simulation version you play. Each download includes its artwork.',
                  )}
                </p>
                <button type="button" onClick={() => setRevision((v) => v + 1)}>
                  {t('Refresh validation')}
                </button>
                <ul>
                  {family.releases.map((v) => (
                    <li key={v.id}>
                      <p>
                        <RichMessage
                          source={'{slot0} · {slot1}'}
                          slots={{
                            slot0: (
                              <strong>{new Date(v.createdAt).toLocaleString(getLocale())}</strong>
                            ),
                            slot1: statusLabel(v.status),
                          }}
                        />
                      </p>
                      <details>
                        <summary>
                          <RichMessage
                            source={'Simulation {slot0}'}
                            slots={{ slot0: v.simVersion.split('-').slice(0, 2).join('-') }}
                          />
                        </summary>
                        <p>
                          <RichMessage
                            source={'Full compatibility key: {slot0}'}
                            slots={{ slot0: <code>{v.simVersion}</code> }}
                          />
                        </p>
                      </details>
                      {v.error && <p role="status">{v.error}</p>}
                      {v.status === 'valid' && !family.hidden && (
                        <div className="building-actions">
                          <a
                            className="button"
                            href={`/api/v1/buildings/${family.id}/releases/${v.id}/archive`}
                          >
                            {t('Download family')}
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
                              {t('Fork into Studio')}
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
                      {t('Report a problem')}
                      <textarea name="reason" required maxLength={2000} />
                    </label>
                    <button disabled={busy}>{t('Send report')}</button>
                  </form>
                )}
              </>
            ) : null
          }
        </Loaded>
      ) : (
        <>
          <LibraryFilters
            onSubmit={(event) => {
              event.preventDefault();
              applySearch();
            }}
          >
            <LibraryField label={t('Search buildings')} search>
              <input
                type="search"
                value={search}
                maxLength={128}
                onChange={(e) => setSearch(e.target.value)}
              />
            </LibraryField>
            <LibraryField label={t('Sort')}>
              <select
                value={sort}
                onChange={(e) => {
                  setSort(e.target.value);
                  setCursor(undefined);
                }}
              >
                <option value="updated">{t('Recently updated')}</option>
                <option value="newest">{t('Newest')}</option>
                <option value="likes">{t('Most liked')}</option>
                <option value="downloads">{t('Most downloaded')}</option>
              </select>
            </LibraryField>
            <button type="button" onClick={reset}>
              {t('Reset filters')}
            </button>
          </LibraryFilters>
          <LibraryResults count={(data) => data.items.length} load={list} retry={list.reload}>
            {(data) => (
              <>
                {data.items.length ? (
                  <LibraryGrid>
                    {data.items.map((family) => {
                      const artwork =
                        !family.hidden &&
                        family.releases.find(
                          (release) => release.status === 'valid' && release.artworkHash,
                        );
                      return (
                        <LibraryCard key={family.id}>
                          <Link to={'/buildings/' + family.id}>
                            {artwork ? (
                              <img
                                className="library-preview"
                                src={`/api/v1/buildings/${family.id}/releases/${artwork.id}/thumbnail`}
                                alt=""
                                loading="lazy"
                              />
                            ) : (
                              <div className="library-preview library-preview-placeholder">
                                <GameArt name="inn" size={72} />
                              </div>
                            )}
                            <h2>{family.name}</h2>
                          </Link>
                          <p className="library-metadata">
                            <RichMessage
                              source={'By {slot0} · {slot1} likes · {slot2}'}
                              slots={{
                                slot0: family.owner.displayName,
                                slot1: family.likes,
                                slot2: availability(family),
                              }}
                            />
                          </p>
                          <p className="library-description">{family.description}</p>
                        </LibraryCard>
                      );
                    })}
                  </LibraryGrid>
                ) : (
                  <LibraryEmpty
                    art="inn"
                    action={
                      query ? (
                        <button onClick={reset}>{t('Clear filters')}</button>
                      ) : (
                        <Link className="btn primary" to="/building-studio">
                          {t('Create a building family')}
                        </Link>
                      )
                    }
                  >
                    {query ? t('No results match these filters.') : t('No families found.')}
                  </LibraryEmpty>
                )}
                {data.nextCursor && (
                  <button type="button" onClick={() => setCursor(data.nextCursor)}>
                    {t('Next page')}
                  </button>
                )}
                {cursor && (
                  <button type="button" onClick={() => setCursor(undefined)}>
                    {t('First page')}
                  </button>
                )}
              </>
            )}
          </LibraryResults>
        </>
      )}
    </div>
  );
}
