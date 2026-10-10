import { statusLabel } from '../i18n.tsx';
import { t, tp, useLocale, RichMessage } from '../i18n.tsx';
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
import { MapLibraryTabs } from './Generators.tsx';
// Map catalog: browse public maps (filters, sorting), my maps, a map's page
// (preview, versions, like, report, owner edits and new versions) and upload.
import { useEffect, useRef, useState, type FormEvent } from 'react';
import type { MapDetail as MapDetailDoc, MapInfo, MapVisibility } from '@glob2/protocol';
import { ApiError, api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { ErrorNotice, Loaded, MapImage, PlayerLink, TableWrap } from '../components/common.tsx';
import { MapPreview } from '../components/MapPreview.tsx';
import { date } from '../format.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

const VISIBILITY_TEXT: Record<MapVisibility, string> = {
  public: 'Public: listed in the catalog',
  unlisted: 'Unlisted: anyone with the link',
  private: 'Private: only you',
};

function MapCard({ map }: { map: MapInfo }) {
  useLocale();
  const v = map.latestVersion;
  return (
    <LibraryCard className="map-card" to={`/maps/${map.id}`} testId="map-card">
      <MapImage src={v?.previewUrl} alt="" />
      <span className="name ell">{map.title}</span>
      <span className="caption ell">
        <RichMessage
          source={'{slot0}{slot1} · ♥ {slot2}{slot3}'}
          slots={{
            slot0: v?.width && v.height ? `${v.width}×${v.height}` : t('size unknown'),
            slot1: v?.teamCount ? tp(' · {count} team', ' · {count} teams', v.teamCount) : '',
            slot2: map.stats.likes,
            slot3: <span className="sr-only"> {t(' likes')}</span>,
          }}
        />
      </span>
      {(map.hidden || map.visibility !== 'public') && (
        <div style={{ padding: '0 var(--sp-1)' }}>
          {map.hidden && <span className="badge bad">{t('hidden')}</span>}{' '}
          {map.visibility !== 'public' && (
            <span className="badge">{statusLabel(map.visibility)}</span>
          )}
        </div>
      )}
    </LibraryCard>
  );
}

/** Same launch intent for browser games, desktop URL handlers and mobile app links. */
export function playMapUrl(
  mapId: string,
  hash: string,
  title: string,
  mode: 'local' | 'multiplayer' = 'multiplayer',
): string {
  const query = new URLSearchParams({
    map: mapId,
    version: hash,
    title: title.slice(0, 128),
    mode,
  });
  return `/play/?${query.toString()}`;
}

function MapPlayDialog({
  mapId,
  hash,
  title,
  close,
}: {
  mapId: string;
  hash: string;
  title: string;
  close: () => void;
}) {
  useLocale();
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const element = dialog.current;
    const opener = document.activeElement;
    element?.showModal();
    return () => {
      element?.close();
      if (opener instanceof HTMLElement) opener.focus();
    };
  }, []);
  return (
    <dialog
      ref={dialog}
      className="map-play-dialog"
      aria-labelledby="map-play-heading"
      onCancel={close}
      onClick={(event) => {
        if (event.target === event.currentTarget) close();
      }}
    >
      <div className="map-play-head">
        <div>
          <h2 id="map-play-heading">{t('How would you like to play?')}</h2>
          <p>{title}</p>
        </div>
        <button aria-label={t('Close play choices')} onClick={close}>
          {t('✕')}
        </button>
      </div>
      <div className="map-play-choices">
        {(['local', 'multiplayer'] as const).map((mode) => {
          const url = playMapUrl(mapId, hash, title, mode);
          const query = new URLSearchParams(url.split('?')[1]);
          query.set('instance', window.location.origin);
          return (
            <div className={`map-play-choice ${mode}`} key={mode}>
              <a className="map-play-card" href={url}>
                <div className="map-play-art" aria-hidden="true">
                  <svg viewBox="0 0 300 160" fill="none">
                    {mode === 'local' ? (
                      <>
                        <rect x="52" y="12" width="196" height="120" rx="12" />
                        <path d="M120 132v16h60v-16M98 148h104" />
                      </>
                    ) : (
                      <>
                        <path d="M150 44L64 116M150 44l86 72M64 116h172" strokeDasharray="7 7" />
                        <circle cx="150" cy="44" r="38" />
                        <circle cx="64" cy="116" r="38" />
                        <circle cx="236" cy="116" r="38" />
                      </>
                    )}
                  </svg>
                  <GameArt
                    name="swarm"
                    size={mode === 'local' ? 84 : 58}
                    className="play-colony center"
                  />
                  {mode === 'multiplayer' && (
                    <>
                      <GameArt name="swarm" size={58} className="play-colony left" />
                      <GameArt name="swarm" size={58} className="play-colony right" />
                    </>
                  )}
                </div>
                <strong>
                  {mode === 'local' ? t('Play Locally in Custom Game') : t('Play in Multiplayer')}
                </strong>
                <span>
                  {mode === 'local'
                    ? t('Set up your colonies and AI opponents.')
                    : t('Create a new room with this map and invite friends.')}
                </span>
              </a>
              <a className="map-play-native" href={`glob2://play?${query.toString()}`}>
                {t('Open in installed app')}
              </a>
            </div>
          );
        })}
      </div>
    </dialog>
  );
}

const SORTS = [
  { id: 'recent', name: 'Newest' },
  { id: 'likes', name: 'Most liked' },
  { id: 'plays', name: 'Most played' },
  { id: 'downloads', name: 'Most downloaded' },
];

export function Maps({ mine }: { mine: boolean }) {
  useLocale();
  const { account } = useSession();
  const [q, setQ] = useState('');
  const [query, setQuery] = useState('');
  const [teams, setTeams] = useState('');
  const [size, setSize] = useState('');
  const [madeWith, setMadeWith] = useState('');
  const [sort, setSort] = useState('recent');
  const [more, setMore] = useState(0);
  const load = useLoad(
    async (signal) => {
      if (mine && !account) return { items: [] as MapInfo[], cursor: undefined };
      const items: MapInfo[] = [];
      let cursor: string | undefined;
      for (let i = 0; i <= more; i++) {
        const page = await api.maps(
          {
            ...(mine ? { owner: 'me' } : {}),
            q: query,
            teams,
            ...(size ? { minSide: size, maxSide: size } : {}),
            madeWith,
            sort,
            cursor,
            limit: 24,
          },
          signal,
        );
        items.push(...page.items);
        cursor = page.nextCursor;
        if (!cursor) break;
      }
      return { items, cursor };
    },
    [mine, account?.id, query, teams, size, madeWith, sort, more],
  );
  const applyQuery = (value: string) => {
    setQuery(value);
    setMore(0);
  };
  const applySearch = useLibrarySearch(q, query, applyQuery);
  const reset = () => {
    setQ('');
    setQuery('');
    setTeams('');
    setSize('');
    setMadeWith('');
    setSort('recent');
    setMore(0);
  };
  const filtered = Boolean(query || teams || size || madeWith);
  return (
    <section className="library-page">
      <LibraryHeader
        art="explorationFlag"
        title={mine ? t('My maps') : t('Maps')}
        description={
          mine
            ? t('Maps you shared, whatever their visibility.')
            : t('Maps players shared. Rooms play them by version, so everyone loads the same file.')
        }
        actions={
          <>
            {account && (
              <Link className="btn primary" to="/maps/new">
                {t('Upload a map')}
              </Link>
            )}
            <Link className="btn" to="/map-studio">
              {t('Build in AI Map Studio')}
            </Link>
          </>
        }
      />
      <div className="library-navigation">
        <LibraryNav label={t('Map lists')}>
          <Link to="/maps" className={mine ? '' : 'on'} aria-current={mine ? undefined : 'page'}>
            {t('Browse')}
          </Link>
          <Link
            to="/maps/mine"
            className={mine ? 'on' : ''}
            aria-current={mine ? 'page' : undefined}
          >
            {t('My maps')}
          </Link>
        </LibraryNav>
        <MapLibraryTabs />
      </div>
      {mine && account === null ? (
        <div className="notice">
          <RichMessage
            source={'{slot0} to see and share your maps.'}
            slots={{ slot0: <a href="/signin">{t('Sign in')}</a> }}
          />
        </div>
      ) : (
        <>
          <LibraryFilters
            activeExtra={Number(Boolean(size)) + Number(Boolean(madeWith))}
            extra={
              <>
                <LibraryField label={t('Size')}>
                  <select
                    aria-label={t('Size')}
                    value={size}
                    onChange={(e) => {
                      setSize(e.target.value);
                      setMore(0);
                    }}
                  >
                    <option value="">{t('Any size')}</option>
                    {[64, 128, 256, 512].map((n) => (
                      <option key={n} value={n}>
                        <RichMessage source={'{slot0}×{slot1}'} slots={{ slot0: n, slot1: n }} />
                      </option>
                    ))}
                  </select>
                </LibraryField>
                <LibraryField label={t('Made with')}>
                  <select
                    aria-label={t('Made with')}
                    value={madeWith}
                    onChange={(e) => {
                      setMadeWith(e.target.value);
                      setMore(0);
                    }}
                  >
                    <option value="">{t('Hand-made or generated')}</option>
                    <option value="hand">{t('Hand-made')}</option>
                    <option value="generator">{t('Generated')}</option>
                  </select>
                </LibraryField>
              </>
            }
            onSubmit={(event) => {
              event.preventDefault();
              applySearch();
            }}
          >
            <LibraryField label={t('Search maps')} search>
              <input
                type="search"
                aria-label={t('Search maps')}
                placeholder={t('Search titles')}
                value={q}
                onChange={(e) => setQ(e.target.value)}
              />
            </LibraryField>
            <LibraryField label={t('Teams')}>
              <select
                aria-label={t('Teams')}
                value={teams}
                onChange={(e) => {
                  setTeams(e.target.value);
                  setMore(0);
                }}
              >
                <option value="">{t('Any teams')}</option>
                {[2, 3, 4, 5, 6, 8, 12].map((n) => (
                  <option key={n} value={n}>
                    <RichMessage source={'{slot0} teams'} slots={{ slot0: n }} />
                  </option>
                ))}
              </select>
            </LibraryField>
            <LibraryField label={t('Sort')}>
              <select
                aria-label={t('Sort')}
                value={sort}
                onChange={(e) => {
                  setSort(e.target.value);
                  setMore(0);
                }}
              >
                {SORTS.map((s) => (
                  <option key={s.id} value={s.id}>
                    {t(s.name)}
                  </option>
                ))}
              </select>
            </LibraryField>
            {(filtered || q || sort !== 'recent') && (
              <button type="button" onClick={reset}>
                {t('Reset filters')}
              </button>
            )}
          </LibraryFilters>
          <LibraryResults count={(data) => data.items.length} load={load} retry={load.reload}>
            {(data) =>
              data.items.length === 0 ? (
                <LibraryEmpty art="explorationFlag">
                  {filtered
                    ? t('No maps match these filters.')
                    : mine
                      ? t('You have not shared any maps yet.')
                      : t('No shared maps yet. Be the first to share one.')}
                  {filtered && <button onClick={reset}>{t('Clear filters')}</button>}
                  {!filtered && (
                    <p>
                      {account ? (
                        <Link className="btn primary" to="/maps/new">
                          {t('Upload a map')}
                        </Link>
                      ) : (
                        <a className="btn" href="/signin">
                          {t('Sign in to upload a map')}
                        </a>
                      )}
                    </p>
                  )}
                </LibraryEmpty>
              ) : (
                <>
                  <LibraryGrid>
                    {data.items.map((map) => (
                      <MapCard key={map.id} map={map} />
                    ))}
                  </LibraryGrid>
                  {data.cursor && (
                    <button
                      className="small"
                      style={{ marginTop: 'var(--sp-3)' }}
                      onClick={() => setMore(more + 1)}
                    >
                      {t('Show more')}
                    </button>
                  )}
                </>
              )
            }
          </LibraryResults>
        </>
      )}
    </section>
  );
}

function ReportForm({ mapId, onDone }: { mapId: string; onDone: () => void }) {
  useLocale();
  const [reason, setReason] = useState('broken');
  const [details, setDetails] = useState('');
  const [error, setError] = useState<Error>();
  const submit = async (event: FormEvent) => {
    event.preventDefault();
    try {
      await api.report(mapId, reason, details);
      onDone();
    } catch (e) {
      setError(e as Error);
    }
  };
  return (
    <form className="card" onSubmit={submit} style={{ marginTop: 10 }}>
      <h2 className="card-title">{t('Report this map')}</h2>
      <label className="field">
        {t('Reason')}
        <select value={reason} onChange={(e) => setReason(e.target.value)}>
          <option value="broken">{t('It is broken or unplayable')}</option>
          <option value="offensive">{t('It is offensive')}</option>
          <option value="copyright">{t('It copies someone else’s work')}</option>
          <option value="other">{t('Something else')}</option>
        </select>
      </label>
      <label className="field">
        {t('Details')}
        <textarea maxLength={2000} value={details} onChange={(e) => setDetails(e.target.value)} />
      </label>
      {error && <ErrorNotice error={error} />}
      <button className="primary" type="submit">
        {t('Send report')}
      </button>
    </form>
  );
}

function OwnerTools({ detail, reload }: { detail: MapDetailDoc; reload: () => void }) {
  useLocale();
  const { navigate } = useRouter();
  const map = detail.map;
  const [title, setTitle] = useState(map.title);
  const [description, setDescription] = useState(map.description);
  const [visibility, setVisibility] = useState<MapVisibility>(map.visibility);
  const [file, setFile] = useState<File>();
  const [notes, setNotes] = useState('');
  const [message, setMessage] = useState<string>();
  const [error, setError] = useState<Error>();
  const run = async (action: () => Promise<unknown>, done: string) => {
    setError(undefined);
    setMessage(undefined);
    try {
      await action();
      setMessage(done);
      reload();
    } catch (e) {
      setError(e as Error);
    }
  };
  return (
    <div className="grid2" style={{ marginTop: 12 }}>
      <form
        className="card"
        onSubmit={(e) => {
          e.preventDefault();
          void run(() => api.updateMap(map.id, { title, description, visibility }), 'Saved.');
        }}
      >
        <h3>{t('Edit')}</h3>
        <label className="field">
          {t('Title')}
          <input
            value={title}
            maxLength={128}
            required
            onChange={(e) => setTitle(e.target.value)}
          />
        </label>
        <label className="field">
          {t('Description')}
          <textarea
            value={description}
            maxLength={4000}
            onChange={(e) => setDescription(e.target.value)}
          />
        </label>
        <label className="field">
          {t('Visibility')}
          <select
            value={visibility}
            onChange={(e) => setVisibility(e.target.value as MapVisibility)}
          >
            {(Object.keys(VISIBILITY_TEXT) as MapVisibility[]).map((v) => (
              <option key={v} value={v}>
                {VISIBILITY_TEXT[v]}
              </option>
            ))}
          </select>
        </label>
        <div className="toolbar">
          <button className="primary" type="submit">
            {t('Save')}
          </button>
          <button
            type="button"
            className="danger"
            onClick={() => {
              if (
                window.confirm(t('Delete “{value0}” and all its versions?', { value0: map.title }))
              ) {
                void api.deleteMap(map.id).then(() => navigate('/maps/mine'), setError);
              }
            }}
          >
            {t('Delete map')}
          </button>
        </div>
      </form>
      {map.authoring?.kind === 'ai' ? (
        <div className="card">
          <h3>{t('AI generated map')}</h3>
          <p>
            {t(
              'This version keeps its original map. Create a revision in AI Map Studio or upload an edited copy as a new map.',
            )}
          </p>
          <Link to="/map-studio">{t('Open AI Map Studio')}</Link>
        </div>
      ) : (
        <form
          className="card"
          onSubmit={(e) => {
            e.preventDefault();
            if (file) {
              void run(
                () => api.uploadVersion(map.id, file, notes),
                'Uploaded. The server checks the file and draws a preview; this takes a moment.',
              );
            }
          }}
        >
          <h3>{t('New version')}</h3>
          <label className="field">
            {t('Map file (.map or .map.gz)')}
            <input
              type="file"
              accept=".map,.gz,.map.gz"
              onChange={(e) => setFile(e.target.files?.[0])}
            />
          </label>
          <label className="field">
            {t('What changed')}
            <input value={notes} maxLength={2000} onChange={(e) => setNotes(e.target.value)} />
          </label>
          <button type="submit" disabled={!file}>
            {t('Upload version')}
          </button>
        </form>
      )}
      {(message || error) && (
        <div style={{ gridColumn: '1 / -1' }}>
          {message && <div className="notice">{message}</div>}
          {error && <ErrorNotice error={error} />}
        </div>
      )}
    </div>
  );
}

export function MapPage({ id }: { id: string }) {
  useLocale();
  const { account } = useSession();
  const { location } = useRouter();
  const uploadFailed = location.search.get('upload') === 'failed';
  const load = useLoad((signal) => api.map(id, signal), [id]);
  const [reporting, setReporting] = useState(false);
  const [choosingPlay, setChoosingPlay] = useState(false);
  const [reported, setReported] = useState(false);
  const [error, setError] = useState<Error>();
  return (
    <Loaded load={load} page="Map">
      {(detail) => {
        const { map, viewer } = detail;
        const v = map.latestVersion;
        const toggleLike = async () => {
          try {
            await api.like(map.id, !viewer.liked);
            load.reload();
          } catch (e) {
            setError(e as Error);
          }
        };
        const moderate = async () => {
          try {
            if (map.hidden) await api.unhideMap(map.id);
            else {
              const reason = window.prompt(t('Why hide this map? (shown to its owner)'));
              if (!reason) return;
              await api.hideMap(map.id, reason);
            }
            load.reload();
          } catch (e) {
            setError(e as Error);
          }
        };
        return (
          <>
            <div className="page-head">
              <div className="grow">
                <h1 data-testid="map-title">{map.title}</h1>
                <div className="sub">
                  {t('by ')}
                  <PlayerLink account={map.owner} /> {t(' · updated ')}
                  {date(map.updatedAt)} {t(' · ')}{' '}
                  {map.madeWith === 'generator' ? t('generated') : t('hand-made')}{' '}
                  {map.visibility !== 'public' && (
                    <span className="badge">{statusLabel(map.visibility)}</span>
                  )}{' '}
                  {map.hidden && <span className="badge bad">{t('hidden by a moderator')}</span>}
                </div>
              </div>
            </div>
            {uploadFailed && detail.versions.length === 0 && (
              <div className="notice error" style={{ marginBottom: 10 }}>
                {t('The map was created but its file did not upload. Upload it again below.')}
              </div>
            )}
            {map.hidden && map.hiddenReason && (
              <div className="notice warn" style={{ marginBottom: 10 }}>
                <RichMessage source={'Hidden: {slot0}'} slots={{ slot0: map.hiddenReason }} />
              </div>
            )}
            <div className="map-hero">
              <div className="preview">
                {/* CLI exports include a two-pixel Glob2Style frame. */}
                <MapPreview
                  src={v?.previewUrl}
                  alt={t('Preview of {value0}', { value0: map.title })}
                  frameInset={2}
                />
              </div>
              <div>
                <div className="tiles">
                  <div className="tile">
                    <div className="caption">{t('Size')}</div>
                    <div className="v">{v?.width && v.height ? `${v.width}×${v.height}` : '–'}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">{t('Teams')}</div>
                    <div className="v">{v?.teamCount ?? '–'}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">{t('Plays')}</div>
                    <div className="v">{map.stats.plays}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">{t('Likes')}</div>
                    <div className="v" data-testid="likes">
                      {map.stats.likes}
                    </div>
                  </div>
                </div>
                {map.description && (
                  <p style={{ whiteSpace: 'pre-wrap', margin: 'var(--sp-4) 0 0' }}>
                    {map.description}
                  </p>
                )}
                <div className="toolbar" style={{ marginTop: 'var(--sp-4)' }}>
                  {v && v.validation === 'valid' && (
                    <>
                      <button
                        className="primary"
                        onClick={() => setChoosingPlay(true)}
                        aria-haspopup="dialog"
                        aria-describedby="play-map-note"
                      >
                        {t('Play this map')}
                      </button>
                      {choosingPlay && (
                        <MapPlayDialog
                          mapId={map.id}
                          hash={v.hash}
                          title={map.title}
                          close={() => setChoosingPlay(false)}
                        />
                      )}
                      <a className="btn" href={v.downloadUrl} download>
                        {t('Download')}
                      </a>
                    </>
                  )}
                  {account ? (
                    <>
                      <button onClick={() => void toggleLike()} aria-pressed={viewer.liked}>
                        {viewer.liked ? t('♥ Liked') : t('♡ Like')}
                      </button>
                      {!viewer.owner && (
                        <button
                          onClick={() => setReporting(!reporting)}
                          disabled={viewer.reported || reported}
                        >
                          {viewer.reported || reported ? t('Reported') : t('Report')}
                        </button>
                      )}
                    </>
                  ) : (
                    <a className="btn" href="/signin">
                      {t('Sign in to like or report')}
                    </a>
                  )}
                  {viewer.moderator && (
                    <button className="danger" onClick={() => void moderate()}>
                      {map.hidden ? t('Unhide') : t('Hide')}
                    </button>
                  )}
                </div>
                {v && v.validation === 'valid' && (
                  <p className="caption" id="play-map-note">
                    {t('Choose a local custom game or a multiplayer room with this map.')}
                  </p>
                )}
                {error && <ErrorNotice error={error} />}
                {reporting && !reported && (
                  <ReportForm
                    mapId={map.id}
                    onDone={() => {
                      setReported(true);
                      setReporting(false);
                    }}
                  />
                )}
              </div>
            </div>
            <h2>{t('Versions')}</h2>
            <TableWrap label={t('Versions of {value0}', { value0: map.title })}>
              <table className="data">
                <caption className="sr-only">
                  <RichMessage source={'Versions of {slot0}'} slots={{ slot0: map.title }} />
                </caption>
                <thead>
                  <tr>
                    <th>{t('Uploaded')}</th>
                    <th>{t('Status')}</th>
                    <th className="hide-phone">{t('Notes')}</th>
                    <th className="num">{t('File')}</th>
                  </tr>
                </thead>
                <tbody>
                  {detail.versions.map((version) => (
                    <tr key={version.hash}>
                      <td>{date(version.createdAt)}</td>
                      <td>
                        {version.validation === 'valid' ? (
                          <span className="badge ok">{t('valid')}</span>
                        ) : version.validation === 'pending' ? (
                          <span className="badge">{t('checking…')}</span>
                        ) : (
                          <span className="badge bad" title={version.reason}>
                            {t('invalid')}
                          </span>
                        )}
                      </td>
                      <td className="caption hide-phone">
                        {version.notes || version.reason || ''}
                        {version.generatorProvenance && (
                          <p>
                            <Link
                              to={`/generators/${version.generatorProvenance.generator.libraryId}`}
                            >
                              <RichMessage
                                source={'{slot0} · revision {slot1}'}
                                slots={{
                                  slot0: version.generatorProvenance.verified
                                    ? t('Generated by')
                                    : t('Author-reported generator'),
                                  slot1: version.generatorProvenance.generator.revision,
                                }}
                              />
                            </Link>
                          </p>
                        )}
                      </td>
                      <td className="num">
                        {version.validation === 'valid' && (
                          <a href={version.downloadUrl} download>
                            <RichMessage
                              source={'Download{slot0}'}
                              slots={{
                                slot0: <span className="sr-only"> {t(' this version')}</span>,
                              }}
                            />
                          </a>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </TableWrap>
            {detail.versions.some((version) => version.setCredits?.length) && (
              <section
                aria-label={t('Custom content credits')}
                style={{ overflowWrap: 'anywhere' }}
              >
                <h2>{t('Custom content credits')}</h2>
                {detail.versions.map((version) =>
                  version.setCredits?.length ? (
                    <details key={version.hash}>
                      <summary>
                        <RichMessage
                          source={'Map version uploaded {slot0}'}
                          slots={{ slot0: date(version.createdAt) }}
                        />
                      </summary>
                      {version.setCredits.map((credit) => (
                        <article key={credit.versionId}>
                          <h3>
                            <RichMessage
                              source={'{slot0} · {slot1}'}
                              slots={{ slot0: credit.title, slot1: credit.license }}
                            />
                          </h3>
                          <p>
                            <RichMessage
                              source={'Imported release {slot0}.'}
                              slots={{ slot0: credit.versionId }}
                            />
                          </p>
                          <ul>
                            {credit.authors.map((author, index) => (
                              <li key={index}>
                                <RichMessage
                                  source={'{slot0} · {slot1}{slot2}'}
                                  slots={{
                                    slot0: author.author,
                                    slot1: author.license,
                                    slot2: author.source && (
                                      <span>
                                        {' '}
                                        {t(' · ')}
                                        {author.source}
                                      </span>
                                    ),
                                  }}
                                />
                              </li>
                            ))}
                          </ul>
                          <Link to={'/sets/' + credit.setId}>{t('Source set')}</Link>
                        </article>
                      ))}
                    </details>
                  ) : null,
                )}
              </section>
            )}
            {viewer.owner && <OwnerTools detail={detail} reload={load.reload} />}
          </>
        );
      }}
    </Loaded>
  );
}

/** "SmallForTwo.map.gz" -> "SmallForTwo". */
export function titleFromFileName(name: string): string {
  return name
    .replace(/(\.(map|gz))+$/i, '')
    .replace(/[_]+/g, ' ')
    .trim()
    .slice(0, 128);
}

const CHECK_TIMEOUT_MS = 120_000;

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

export function MapUpload() {
  useLocale();
  const { account } = useSession();
  const { navigate } = useRouter();
  const [title, setTitle] = useState('');
  const [description, setDescription] = useState('');
  const [visibility, setVisibility] = useState<MapVisibility>('unlisted');
  const [madeWith, setMadeWith] = useState('hand');
  const [file, setFile] = useState<File>();
  const [phase, setPhase] = useState<'idle' | 'checking' | 'saving'>('idle');
  const [fileError, setFileError] = useState<string>();
  const [error, setError] = useState<Error>();
  const fileInput = useRef<HTMLInputElement>(null);
  const head = (
    <div className="page-head">
      <GameArt name="explorationFlag" size={72} className="head-art" />
      <div className="grow">
        <h1>{t('Upload a map')}</h1>
        <p className="sub">{t('Share a map you made in the editor or with a generator.')}</p>
      </div>
    </div>
  );
  if (account === null) {
    return (
      <>
        {head}
        <div className="notice">
          <RichMessage
            source={'{slot0} to share maps.'}
            slots={{ slot0: <a href="/signin">{t('Sign in')}</a> }}
          />
        </div>
      </>
    );
  }
  const busy = phase !== 'idle';
  const fileProblem = (message: string) => {
    setFileError(message);
    fileInput.current?.focus();
  };
  const submit = async (event: FormEvent) => {
    event.preventDefault();
    if (!file) {
      fileProblem(t('Choose a map file first: a .map or .map.gz file from the game.'));
      return;
    }
    setError(undefined);
    setFileError(undefined);
    try {
      // 1. The game checks the file. Nothing is created unless it loads.
      setPhase('checking');
      let checked = await api.checkMapFile(file, file.name);
      const started = Date.now();
      while (checked.status === 'pending') {
        if (Date.now() - started > CHECK_TIMEOUT_MS) {
          throw new Error(
            'Checking the map is taking longer than usual. Nothing was created; please try again in a few minutes.',
          );
        }
        await sleep(1000);
        checked = await api.checkedFile(checked.id);
      }
      if (checked.status !== 'valid') {
        fileProblem(checked.reason ?? "The game couldn't load this map.");
        return;
      }
      // 2. Only now: the map page and its first version (the same, already checked file).
      setPhase('saving');
      const map = await api.createMap({ title, description, visibility, madeWith });
      const uploaded = await api.uploadVersion(map.id, file).then(
        () => true,
        (e: unknown) => {
          if (!(e instanceof ApiError)) throw e;
          return false;
        },
      );
      navigate(`/maps/${map.id}${uploaded ? '' : '?upload=failed'}`);
    } catch (e) {
      // Problems with the file itself (not a map, too big, newer version…) belong to the file field.
      const details = e instanceof ApiError ? (e.body?.details as { problem?: string }) : undefined;
      if (e instanceof ApiError && (details?.problem || e.status === 413)) fileProblem(e.message);
      else setError(e as Error);
    } finally {
      setPhase('idle');
    }
  };
  return (
    <>
      {head}
      <form className="card upload-form" onSubmit={(e) => void submit(e)} noValidate>
        <label className="field">
          {t('Map file (.map or .map.gz, from the game or its map editor)')}
          <input
            ref={fileInput}
            type="file"
            accept=".map,.gz,.map.gz"
            aria-invalid={fileError ? true : undefined}
            aria-describedby="map-file-error"
            onChange={(e) => {
              const f = e.target.files?.[0];
              setFile(f);
              setFileError(undefined);
              if (f && !title) setTitle(titleFromFileName(f.name));
            }}
          />
        </label>
        <p id="map-file-error" className="field-error" role="alert">
          {fileError}
        </p>
        <label className="field">
          {t('Title')}
          <input
            value={title}
            maxLength={128}
            required
            onChange={(e) => setTitle(e.target.value)}
          />
        </label>
        <label className="field">
          {t('Description')}
          <textarea
            value={description}
            maxLength={4000}
            onChange={(e) => setDescription(e.target.value)}
          />
        </label>
        <label className="field">
          {t('Visibility')}
          <select
            value={visibility}
            onChange={(e) => setVisibility(e.target.value as MapVisibility)}
          >
            {(Object.keys(VISIBILITY_TEXT) as MapVisibility[]).map((v) => (
              <option key={v} value={v}>
                {VISIBILITY_TEXT[v]}
              </option>
            ))}
          </select>
        </label>
        <label className="field">
          {t('Made')}
          <select value={madeWith} onChange={(e) => setMadeWith(e.target.value)}>
            <option value="hand">{t('In the map editor')}</option>
            <option value="generator">{t('By a map generator')}</option>
          </select>
        </label>
        {error && <ErrorNotice error={error} />}
        <button className="primary" type="submit" disabled={busy}>
          {phase === 'checking'
            ? t('Checking the map…')
            : phase === 'saving'
              ? t('Saving…')
              : t('Upload')}
        </button>
        <p className="sr-only" role="status">
          {phase === 'checking'
            ? t('Checking the map with the game.')
            : phase === 'saving'
              ? t('The map is fine. Saving it.')
              : ''}
        </p>
        <p className="caption">
          {t(
            'The server first loads the file with the game to check it; the map page is created only if it loads. Unlisted maps are reachable by link only; you can make a map public later.',
          )}
        </p>
      </form>
    </>
  );
}
