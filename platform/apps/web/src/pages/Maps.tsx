// Map catalog: browse public maps (filters, sorting), my maps, a map's page
// (preview, versions, like, report, owner edits and new versions) and upload.
import { useRef, useState, type FormEvent } from 'react';
import type { MapDetail as MapDetailDoc, MapInfo, MapVisibility } from '@glob2/protocol';
import { ApiError, api } from '../api.ts';
import { GameArt } from '../art.tsx';
import {
  Empty,
  ErrorNotice,
  Loaded,
  MapImage,
  PlayerLink,
  TableWrap,
} from '../components/common.tsx';
import { date } from '../format.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

const VISIBILITY_TEXT: Record<MapVisibility, string> = {
  public: 'Public: listed in the catalog',
  unlisted: 'Unlisted: anyone with the link',
  private: 'Private: only you',
};

function MapCard({ map }: { map: MapInfo }) {
  const v = map.latestVersion;
  return (
    <Link className="map-card" to={`/maps/${map.id}`} data-testid="map-card">
      <MapImage src={v?.previewUrl} alt="" />
      <span className="name ell">{map.title}</span>
      <span className="caption ell">
        {v?.width && v.height ? `${v.width}×${v.height}` : 'size unknown'}
        {v?.teamCount ? ` · ${v.teamCount} teams` : ''} · ♥ {map.stats.likes}
        <span className="sr-only"> likes</span>
      </span>
      {(map.hidden || map.visibility !== 'public') && (
        <div style={{ padding: '0 var(--sp-1)' }}>
          {map.hidden && <span className="badge bad">hidden</span>}{' '}
          {map.visibility !== 'public' && <span className="badge">{map.visibility}</span>}
        </div>
      )}
    </Link>
  );
}

/**
 * The browser game with this catalog version kept for the next room the player
 * creates (browser/shell.html turns the query into --room-map).
 */
export function playMapUrl(mapId: string, hash: string, title: string): string {
  const query = new URLSearchParams({ map: mapId, version: hash, title: title.slice(0, 128) });
  return `/play/?${query.toString()}`;
}

const SORTS = [
  { id: 'recent', name: 'Newest' },
  { id: 'likes', name: 'Most liked' },
  { id: 'plays', name: 'Most played' },
  { id: 'downloads', name: 'Most downloaded' },
];

export function Maps({ mine }: { mine: boolean }) {
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
  const submit = (event: FormEvent) => {
    event.preventDefault();
    setQuery(q.trim());
    setMore(0);
  };
  const filtered = Boolean(query || teams || size || madeWith);
  return (
    <>
      <div className="page-head">
        <GameArt name="explorationFlag" size={72} className="head-art" />
        <div className="grow">
          <h1>{mine ? 'My maps' : 'Maps'}</h1>
          <p className="sub">
            {mine
              ? 'Maps you shared, whatever their visibility.'
              : 'Maps players shared. Rooms play them by version, so everyone loads the same file.'}
          </p>
        </div>
        <nav className="seg" aria-label="Map lists">
          <Link to="/maps" className={mine ? '' : 'on'} aria-current={mine ? undefined : 'page'}>
            Catalog
          </Link>
          <Link
            to="/maps/mine"
            className={mine ? 'on' : ''}
            aria-current={mine ? 'page' : undefined}
          >
            My maps
          </Link>
        </nav>
        {account && (
          <Link className="btn primary" to="/maps/new">
            Upload a map
          </Link>
        )}
      </div>
      {mine && account === null ? (
        <div className="notice">
          <a href="/signin">Sign in</a> to see and share your maps.
        </div>
      ) : (
        <>
          <form className="filters" onSubmit={submit} role="search">
            <input
              type="search"
              aria-label="Search maps"
              placeholder="Search titles"
              value={q}
              onChange={(e) => setQ(e.target.value)}
            />
            <button type="submit">Search</button>
            <select aria-label="Teams" value={teams} onChange={(e) => setTeams(e.target.value)}>
              <option value="">Any teams</option>
              {[2, 3, 4, 5, 6, 8, 12].map((n) => (
                <option key={n} value={n}>
                  {n} teams
                </option>
              ))}
            </select>
            <select aria-label="Size" value={size} onChange={(e) => setSize(e.target.value)}>
              <option value="">Any size</option>
              {[64, 128, 256, 512].map((n) => (
                <option key={n} value={n}>
                  {n}×{n}
                </option>
              ))}
            </select>
            <select
              aria-label="Made with"
              value={madeWith}
              onChange={(e) => setMadeWith(e.target.value)}
            >
              <option value="">Hand-made or generated</option>
              <option value="hand">Hand-made</option>
              <option value="generator">Generated</option>
            </select>
            <select aria-label="Sort" value={sort} onChange={(e) => setSort(e.target.value)}>
              {SORTS.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.name}
                </option>
              ))}
            </select>
            {(filtered || q || sort !== 'recent') && (
              <button
                type="button"
                onClick={() => {
                  setQ('');
                  setQuery('');
                  setTeams('');
                  setSize('');
                  setMadeWith('');
                  setSort('recent');
                  setMore(0);
                }}
              >
                Reset filters
              </button>
            )}
          </form>
          <Loaded load={load}>
            {(data) =>
              data.items.length === 0 ? (
                <Empty art="explorationFlag">
                  {filtered
                    ? 'No maps match these filters.'
                    : mine
                      ? 'You have not shared any maps yet.'
                      : 'No shared maps yet. Be the first to share one.'}
                  {!filtered && (
                    <p>
                      {account ? (
                        <Link className="btn primary" to="/maps/new">
                          Upload a map
                        </Link>
                      ) : (
                        <a className="btn" href="/signin">
                          Sign in to upload a map
                        </a>
                      )}
                    </p>
                  )}
                </Empty>
              ) : (
                <>
                  <div className="map-grid">
                    {data.items.map((map) => (
                      <MapCard key={map.id} map={map} />
                    ))}
                  </div>
                  {data.cursor && (
                    <button
                      className="small"
                      style={{ marginTop: 'var(--sp-3)' }}
                      onClick={() => setMore(more + 1)}
                    >
                      Show more
                    </button>
                  )}
                </>
              )
            }
          </Loaded>
        </>
      )}
    </>
  );
}

function ReportForm({ mapId, onDone }: { mapId: string; onDone: () => void }) {
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
      <h2 className="card-title">Report this map</h2>
      <label className="field">
        Reason
        <select value={reason} onChange={(e) => setReason(e.target.value)}>
          <option value="broken">It is broken or unplayable</option>
          <option value="offensive">It is offensive</option>
          <option value="copyright">It copies someone else’s work</option>
          <option value="other">Something else</option>
        </select>
      </label>
      <label className="field">
        Details
        <textarea maxLength={2000} value={details} onChange={(e) => setDetails(e.target.value)} />
      </label>
      {error && <ErrorNotice error={error} />}
      <button className="primary" type="submit">
        Send report
      </button>
    </form>
  );
}

function OwnerTools({ detail, reload }: { detail: MapDetailDoc; reload: () => void }) {
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
        <h3>Edit</h3>
        <label className="field">
          Title
          <input
            value={title}
            maxLength={128}
            required
            onChange={(e) => setTitle(e.target.value)}
          />
        </label>
        <label className="field">
          Description
          <textarea
            value={description}
            maxLength={4000}
            onChange={(e) => setDescription(e.target.value)}
          />
        </label>
        <label className="field">
          Visibility
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
            Save
          </button>
          <button
            type="button"
            className="danger"
            onClick={() => {
              if (window.confirm(`Delete “${map.title}” and all its versions?`)) {
                void api.deleteMap(map.id).then(() => navigate('/maps/mine'), setError);
              }
            }}
          >
            Delete map
          </button>
        </div>
      </form>
      {map.authoring?.kind === 'ai' ? (
        <div className="card">
          <h3>AI generated map</h3>
          <p>
            This version keeps its original map. Create a revision in AI Map Studio or upload an
            edited copy as a new map.
          </p>
          <Link to="/map-studio">Open AI Map Studio</Link>
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
          <h3>New version</h3>
          <label className="field">
            Map file (.map or .map.gz)
            <input
              type="file"
              accept=".map,.gz,.map.gz"
              onChange={(e) => setFile(e.target.files?.[0])}
            />
          </label>
          <label className="field">
            What changed
            <input value={notes} maxLength={2000} onChange={(e) => setNotes(e.target.value)} />
          </label>
          <button type="submit" disabled={!file}>
            Upload version
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
  const { account } = useSession();
  const { location } = useRouter();
  const uploadFailed = location.search.get('upload') === 'failed';
  const load = useLoad((signal) => api.map(id, signal), [id]);
  const [reporting, setReporting] = useState(false);
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
              const reason = window.prompt('Why hide this map? (shown to its owner)');
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
                  by <PlayerLink account={map.owner} /> · updated {date(map.updatedAt)} ·{' '}
                  {map.madeWith === 'generator' ? 'generated' : 'hand-made'}{' '}
                  {map.visibility !== 'public' && <span className="badge">{map.visibility}</span>}{' '}
                  {map.hidden && <span className="badge bad">hidden by a moderator</span>}
                </div>
              </div>
            </div>
            {uploadFailed && detail.versions.length === 0 && (
              <div className="notice error" style={{ marginBottom: 10 }}>
                The map was created but its file did not upload. Upload it again below.
              </div>
            )}
            {map.hidden && map.hiddenReason && (
              <div className="notice warn" style={{ marginBottom: 10 }}>
                Hidden: {map.hiddenReason}
              </div>
            )}
            <div className="map-hero">
              <div className="preview">
                <MapImage src={v?.previewUrl} alt={`Preview of ${map.title}`} />
              </div>
              <div>
                <div className="tiles">
                  <div className="tile">
                    <div className="caption">Size</div>
                    <div className="v">{v?.width && v.height ? `${v.width}×${v.height}` : '–'}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">Teams</div>
                    <div className="v">{v?.teamCount ?? '–'}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">Plays</div>
                    <div className="v">{map.stats.plays}</div>
                  </div>
                  <div className="tile">
                    <div className="caption">Likes</div>
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
                      <a
                        className="btn primary"
                        href={playMapUrl(map.id, v.hash, map.title)}
                        aria-describedby="play-map-note"
                      >
                        Play this map
                      </a>
                      <a className="btn" href={v.downloadUrl} download>
                        Download
                      </a>
                    </>
                  )}
                  {account ? (
                    <>
                      <button onClick={() => void toggleLike()} aria-pressed={viewer.liked}>
                        {viewer.liked ? '♥ Liked' : '♡ Like'}
                      </button>
                      {!viewer.owner && (
                        <button
                          onClick={() => setReporting(!reporting)}
                          disabled={viewer.reported || reported}
                        >
                          {viewer.reported || reported ? 'Reported' : 'Report'}
                        </button>
                      )}
                    </>
                  ) : (
                    <a className="btn" href="/signin">
                      Sign in to like or report
                    </a>
                  )}
                  {viewer.moderator && (
                    <button className="danger" onClick={() => void moderate()}>
                      {map.hidden ? 'Unhide' : 'Hide'}
                    </button>
                  )}
                </div>
                {v && v.validation === 'valid' && (
                  <p className="caption" id="play-map-note">
                    Opens the game in your browser; the next room you create plays this map.
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
            <h2>Versions</h2>
            <TableWrap label={`Versions of ${map.title}`}>
              <table className="data">
                <caption className="sr-only">Versions of {map.title}</caption>
                <thead>
                  <tr>
                    <th>Uploaded</th>
                    <th>Status</th>
                    <th className="hide-phone">Notes</th>
                    <th className="num">File</th>
                  </tr>
                </thead>
                <tbody>
                  {detail.versions.map((version) => (
                    <tr key={version.hash}>
                      <td>{date(version.createdAt)}</td>
                      <td>
                        {version.validation === 'valid' ? (
                          <span className="badge ok">valid</span>
                        ) : version.validation === 'pending' ? (
                          <span className="badge">checking…</span>
                        ) : (
                          <span className="badge bad" title={version.reason}>
                            invalid
                          </span>
                        )}
                      </td>
                      <td className="caption hide-phone">
                        {version.notes || version.reason || ''}
                      </td>
                      <td className="num">
                        {version.validation === 'valid' && (
                          <a href={version.downloadUrl} download>
                            Download<span className="sr-only"> this version</span>
                          </a>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </TableWrap>
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
        <h1>Upload a map</h1>
        <p className="sub">Share a map you made in the editor or with a generator.</p>
      </div>
    </div>
  );
  if (account === null) {
    return (
      <>
        {head}
        <div className="notice">
          <a href="/signin">Sign in</a> to share maps.
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
      fileProblem('Choose a map file first: a .map or .map.gz file from the game.');
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
          Map file (.map or .map.gz, from the game or its map editor)
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
          Title
          <input
            value={title}
            maxLength={128}
            required
            onChange={(e) => setTitle(e.target.value)}
          />
        </label>
        <label className="field">
          Description
          <textarea
            value={description}
            maxLength={4000}
            onChange={(e) => setDescription(e.target.value)}
          />
        </label>
        <label className="field">
          Visibility
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
          Made
          <select value={madeWith} onChange={(e) => setMadeWith(e.target.value)}>
            <option value="hand">In the map editor</option>
            <option value="generator">By a map generator</option>
          </select>
        </label>
        {error && <ErrorNotice error={error} />}
        <button className="primary" type="submit" disabled={busy}>
          {phase === 'checking' ? 'Checking the map…' : phase === 'saving' ? 'Saving…' : 'Upload'}
        </button>
        <p className="sr-only" role="status">
          {phase === 'checking'
            ? 'Checking the map with the game.'
            : phase === 'saving'
              ? 'The map is fine. Saving it.'
              : ''}
        </p>
        <p className="caption">
          The server first loads the file with the game to check it; the map page is created only if
          it loads. Unlisted maps are reachable by link only; you can make a map public later.
        </p>
      </form>
    </>
  );
}
