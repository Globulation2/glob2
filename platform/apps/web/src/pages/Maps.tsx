// Map catalog: browse public maps (filters, sorting), my maps, a map's page
// (preview, versions, like, report, owner edits and new versions) and upload.
import { useState, type FormEvent } from 'react';
import type { MapDetail as MapDetailDoc, MapInfo, MapVisibility } from '@glob2/protocol';
import { ApiError, api } from '../api.ts';
import { ErrorNotice, Loaded, MapImage, PlayerLink } from '../components/common.tsx';
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
      <MapImage src={v?.previewUrl} alt={map.title} />
      <div className="ell" style={{ marginTop: 4 }}>
        {map.title}
      </div>
      <div className="caption ell">
        {v?.width && v.height ? `${v.width}×${v.height}` : 'size unknown'}
        {v?.teamCount ? ` · ${v.teamCount} teams` : ''} · ♥ {map.stats.likes}
      </div>
      {(map.hidden || map.visibility !== 'public') && (
        <div>
          {map.hidden && <span className="badge bad">hidden</span>}{' '}
          {map.visibility !== 'public' && <span className="badge">{map.visibility}</span>}
        </div>
      )}
    </Link>
  );
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
  return (
    <>
      <div className="page-head">
        <div className="grow">
          <h1>{mine ? 'My maps' : 'Maps'}</h1>
          <div className="caption">
            {mine
              ? 'Maps you shared, whatever their visibility.'
              : 'Maps players shared. Rooms play them by version, so everyone loads the same file.'}
          </div>
        </div>
        <div className="seg">
          <Link to="/maps" className={mine ? '' : 'on'}>
            Catalog
          </Link>
          <Link to="/maps/mine" className={mine ? 'on' : ''}>
            My maps
          </Link>
        </div>
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
          <form className="toolbar" onSubmit={submit} role="search">
            <input
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
          </form>
          <Loaded load={load}>
            {(data) =>
              data.items.length === 0 ? (
                <div className="list empty">
                  {mine ? 'You have not shared any maps yet.' : 'No maps match.'}
                </div>
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
                      style={{ marginTop: 10 }}
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
      <h3>Report this map</h3>
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
          Map file (.map)
          <input type="file" accept=".map,.gz" onChange={(e) => setFile(e.target.files?.[0])} />
        </label>
        <label className="field">
          What changed
          <input value={notes} maxLength={2000} onChange={(e) => setNotes(e.target.value)} />
        </label>
        <button type="submit" disabled={!file}>
          Upload version
        </button>
      </form>
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
    <Loaded load={load}>
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
                <div className="caption">
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
            <div className="grid2">
              <div>
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
                {map.description && <p style={{ whiteSpace: 'pre-wrap' }}>{map.description}</p>}
                <div className="toolbar" style={{ marginTop: 10 }}>
                  {v && v.validation === 'valid' && (
                    <a className="btn primary" href={v.downloadUrl} download>
                      Download
                    </a>
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
            <div className="table-wrap">
              <table className="data">
                <thead>
                  <tr>
                    <th>Uploaded</th>
                    <th>Status</th>
                    <th className="hide-phone">Notes</th>
                    <th />
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
                            file
                          </a>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {viewer.owner && <OwnerTools detail={detail} reload={load.reload} />}
          </>
        );
      }}
    </Loaded>
  );
}

export function MapUpload() {
  const { account } = useSession();
  const { navigate } = useRouter();
  const [title, setTitle] = useState('');
  const [description, setDescription] = useState('');
  const [visibility, setVisibility] = useState<MapVisibility>('unlisted');
  const [madeWith, setMadeWith] = useState('hand');
  const [file, setFile] = useState<File>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<Error>();
  if (account === null) {
    return (
      <div className="notice">
        <a href="/signin">Sign in</a> to share maps.
      </div>
    );
  }
  const submit = async (event: FormEvent) => {
    event.preventDefault();
    if (!file) return;
    setBusy(true);
    setError(undefined);
    try {
      const map = await api.createMap({ title, description, visibility, madeWith });
      // A failed upload keeps the map: its page shows no version yet and lets
      // the owner upload again.
      const uploaded = await api.uploadVersion(map.id, file).then(
        () => true,
        (e: unknown) => {
          if (!(e instanceof ApiError)) throw e;
          return false;
        },
      );
      navigate(`/maps/${map.id}${uploaded ? '' : '?upload=failed'}`);
    } catch (e) {
      setError(e as Error);
    } finally {
      setBusy(false);
    }
  };
  return (
    <>
      <div className="page-head">
        <h1 className="grow">Upload a map</h1>
      </div>
      <form className="card" onSubmit={(e) => void submit(e)} style={{ maxWidth: 560 }}>
        <label className="field">
          Map file (.map, from the editor’s Save)
          <input
            type="file"
            accept=".map,.gz"
            required
            onChange={(e) => {
              const f = e.target.files?.[0];
              setFile(f);
              if (f && !title) setTitle(f.name.replace(/\.(map|gz)+$/i, '').slice(0, 128));
            }}
          />
        </label>
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
        <button className="primary" type="submit" disabled={busy || !file}>
          {busy ? 'Uploading…' : 'Upload'}
        </button>
        <p className="caption">
          The server loads the file with the game to check it and draw a preview. Unlisted maps are
          reachable by link only; you can make a map public later.
        </p>
      </form>
    </>
  );
}
