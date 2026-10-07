import { useEffect, useRef, useState } from 'react';
import type { MusicMetadata, MusicRelease } from '@glob2/protocol';
import { request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import { MusicValidation } from './Validation.tsx';
import { MOODS, MusicPlayer } from './Player.tsx';
import { useMusicCatalogue } from './useMusicCatalogue.ts';

import { Cover } from './Cover.tsx';
function errorText(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}
function PreviewDialog({ release, close }: { release: MusicRelease; close: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null;
    dialog.current?.showModal();
    const overflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      document.body.style.overflow = overflow;
      previous?.focus();
    };
  }, []);
  return (
    <dialog
      ref={dialog}
      className="music-preview-panel"
      aria-labelledby="music-preview-title"
      onCancel={close}
    >
      <div className="music-heading">
        <h2 id="music-preview-title">{release.metadata.title}</h2>
        <button onClick={close}>Close preview</button>
      </div>
      <MusicPlayer release={release} />
    </dialog>
  );
}
export function MusicLibrary() {
  const { account } = useSession();
  const [query, setQuery] = useState(''),
    [sort, setSort] = useState('likes'),
    [licence, setLicence] = useState(''),
    [ai, setAi] = useState(''),
    [tag, setTag] = useState('');
  const [min, setMin] = useState(10),
    [max, setMax] = useState(900),
    [mine, setMine] = useState(false);
  const [selected, setSelected] = useState<Map<string, string>>(new Map());
  const [preview, setPreview] = useState<MusicRelease | null>(null),
    [notice, setNotice] = useState(''),
    [busy, setBusy] = useState(false);
  const filters = new URLSearchParams({
    q: query,
    sort,
    license: licence,
    ai,
    tag,
    min: String(min),
    max: String(max),
    mine: mine ? '1' : '0',
  }).toString();
  const { items, setItems, next, loading, error, more } = useMusicCatalogue(filters);
  async function downloadSelected() {
    setBusy(true);
    setNotice('');
    try {
      const response = await fetch('/api/v1/music/download', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids: [...selected.keys()] }),
      });
      if (!response.ok) {
        const error = (await response.json()) as { message?: string };
        throw new Error(error.message ?? 'Download failed.');
      }
      const url = URL.createObjectURL(await response.blob());
      const a = document.createElement('a');
      a.href = url;
      a.download = 'glob2-music.zip';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 60_000);
    } catch (e) {
      setNotice(errorText(e));
    } finally {
      setBusy(false);
    }
  }
  async function like(release: MusicRelease) {
    try {
      const updated = await request<MusicRelease>(
        release.liked ? 'DELETE' : 'PUT',
        `/api/v1/music/${release.id}/like`,
      );
      setItems((old) => old.map((r) => (r.id === updated.id ? updated : r)));
    } catch (e) {
      setNotice(errorText(e));
    }
  }
  return (
    <main className="music-library">
      <div className="music-heading">
        <div>
          <p className="eyebrow">COMMUNITY SOUNDTRACKS</p>
          <h1>Music for your colony</h1>
          <p>Three moods. One shared rhythm. Find your next soundtrack.</p>
        </div>
        <Link to="/music-studio" className="button">
          Build in AI Music Studio
        </Link>
        <Link to="/music/new" className="button">
          Share music
        </Link>
      </div>
      <div className="music-filters">
        <label>
          Search
          <input
            type="search"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Title, artist, description…"
          />
        </label>
        <label>
          Sort
          <select value={sort} onChange={(e) => setSort(e.target.value)}>
            <option value="likes">Most liked</option>
            <option value="recent">Newest</option>
            <option value="downloads">Most downloaded</option>
          </select>
        </label>
        <label>
          Licence
          <select value={licence} onChange={(e) => setLicence(e.target.value)}>
            <option value="">All open licences</option>
            <option>CC0-1.0</option>
            <option>CC-BY-4.0</option>
            <option>CC-BY-SA-4.0</option>
          </select>
        </label>
        <label>
          AI disclosure
          <select value={ai} onChange={(e) => setAi(e.target.value)}>
            <option value="">All music</option>
            <option value="false">Not AI-generated</option>
            <option value="true">AI-generated</option>
          </select>
        </label>
        <label>
          Tag
          <input value={tag} onChange={(e) => setTag(e.target.value)} placeholder="forest" />
        </label>
        <label>
          Minimum seconds
          <input
            type="number"
            min="10"
            max="900"
            value={min}
            onChange={(e) => setMin(+e.target.value)}
          />
        </label>
        <label>
          Maximum seconds
          <input
            type="number"
            min="10"
            max="900"
            value={max}
            onChange={(e) => setMax(+e.target.value)}
          />
        </label>
        {account && (
          <label>
            <input type="checkbox" checked={mine} onChange={(e) => setMine(e.target.checked)} /> My
            releases and uploads
          </label>
        )}
      </div>
      {(notice || error) && <p role="alert">{notice || error}</p>}
      {(busy || loading) && <p role="status">Loading…</p>}
      <div className="music-grid">
        {items.map((release) => (
          <article key={release.id} className="music-card">
            <Cover release={release} />
            <div className="music-card-body">
              <Link to={`/music/${release.id}`}>
                <h2>{release.metadata.title}</h2>
              </Link>
              <p>
                {release.metadata.artist} · {Math.round(release.frames / 48000)} seconds
              </p>
              <p className="music-description">{release.metadata.description}</p>
              <p>
                {release.metadata.license}
                {release.metadata.aiGenerated ? ' · AI-generated' : ''}
              </p>
              {release.status !== 'published' && <p>Status: {release.status}</p>}
              <div className="music-card-actions">
                <button
                  disabled={!release.tracks.length}
                  onClick={(event) => {
                    event.currentTarget.focus();
                    setPreview(release);
                  }}
                >
                  Preview
                </button>
                <button
                  disabled={account?.kind !== 'registered' || release.status !== 'published'}
                  aria-label={`${release.liked ? 'Unlike' : 'Like'} ${release.metadata.title} · ${release.likes} likes`}
                  title={
                    account?.kind !== 'registered'
                      ? 'Sign in with a registered account to like music'
                      : undefined
                  }
                  aria-pressed={release.liked}
                  onClick={() => void like(release)}
                >
                  ♥ {release.likes}
                </button>
                {release.tracks.length > 0 && (
                  <a href={`/api/v1/music/${release.id}/download`} download>
                    Download ZIP
                  </a>
                )}
              </div>
              {release.status === 'published' && (
                <label>
                  <input
                    type="checkbox"
                    checked={selected.has(release.id)}
                    disabled={!selected.has(release.id) && selected.size >= 10}
                    onChange={(e) =>
                      setSelected((old) => {
                        const value = new Map(old);
                        if (e.target.checked) value.set(release.id, release.metadata.title);
                        else value.delete(release.id);
                        return value;
                      })
                    }
                  />{' '}
                  Select for download
                </label>
              )}
            </div>
          </article>
        ))}
      </div>
      {!busy && !loading && !items.length && (
        <p>No music found. Try different filters, or share the first set.</p>
      )}
      {next && (
        <button onClick={() => void more()} disabled={busy || loading}>
          Load more
        </button>
      )}
      {selected.size > 0 && (
        <aside className="music-selection" aria-label="Selected music">
          <span>{selected.size} selected · up to 10 sets, 64 MiB</span>
          <button onClick={() => void downloadSelected()} disabled={busy}>
            Download selected
          </button>
          <button onClick={() => setSelected(new Map())}>Clear selection</button>
        </aside>
      )}
      {preview && (
        <PreviewDialog key={preview.id} release={preview} close={() => setPreview(null)} />
      )}
    </main>
  );
}

export function MusicDetail({ id }: { id: string }) {
  const { account } = useSession();
  const data = useLoad(
    (signal) => request<MusicRelease>('GET', `/api/v1/music/${id}`, { signal }),
    [id],
  );
  const [notice, setNotice] = useState(''),
    [reason, setReason] = useState('');
  const [repair, setRepair] = useState('none'),
    [master, setMaster] = useState(false);
  const [busy, setBusy] = useState(false);
  const processing =
    data.status === 'ready' && ['inspecting', 'converting'].includes(data.data.status);
  useEffect(() => {
    if (!processing) return;
    const timer = setInterval(data.reload, 2000);
    return () => clearInterval(timer);
  }, [processing, data.reload]);
  async function action(method: string, suffix: string, body?: unknown) {
    if (busy) return;
    setBusy(true);
    try {
      await request(method, `/api/v1/music/${id}${suffix}`, { body });
      data.reload();
      setNotice('Saved.');
    } catch (e) {
      setNotice(errorText(e));
    } finally {
      setBusy(false);
    }
  }
  if (data.status === 'loading') return <p role="status">Loading music…</p>;
  if (data.status === 'error') return <p role="alert">{data.error.message}</p>;
  const release = data.data,
    owner = account?.id === release.ownerId;
  return (
    <main className="music-detail">
      <Link to="/music">← Music library</Link>
      <section className="music-listening-room">
        <div className="music-detail-heading">
          <Cover release={release} />
          <div>
            <span className="music-eyebrow">A SOUNDTRACK FOR YOUR COLONY</span>
            <h1>{release.metadata.title}</h1>
            <p className="music-artist">By {release.metadata.artist}</p>
            <p className="music-description-full">{release.metadata.description}</p>
            <p className="music-attribution">
              {release.metadata.license} ·{' '}
              {release.metadata.aiGenerated ? 'AI-generated' : 'Not AI-generated'}
            </p>
          </div>
        </div>
        {notice && <p role="status">{notice}</p>}
        {release.error && <p role="alert">{release.error}</p>}
        {release.status !== 'published' && (
          <p className="music-release-state">Status: {release.status}</p>
        )}
        {release.tracks.length === 3 && (
          <>
            <MusicPlayer key={release.id} release={release} />
            <div className="music-downloads">
              <a className="btn" href={`/api/v1/music/${id}/download`} download>
                Download set
              </a>
              <details>
                <summary>Individual moods</summary>
                <div className="music-card-actions">
                  {release.tracks.map((track, i) => (
                    <a key={track.mood} href={track.url} download={`a${i + 1}.opus`}>
                      Download {MOODS[i]}
                    </a>
                  ))}
                </div>
              </details>
            </div>
          </>
        )}
      </section>
      {(release.validation || release.warnings.length > 0) && (
        <MusicValidation checks={release.validation ?? []} warnings={release.warnings} />
      )}
      <details className="music-credits">
        <summary>Credits &amp; sources</summary>
        <p>{release.metadata.credits}</p>
        <p>License: {release.metadata.license}</p>
        {release.metadata.sources.map((source) => (
          <p key={source}>
            <a href={source} rel="noreferrer">
              {source}
            </a>
          </p>
        ))}
      </details>
      {owner && !release.generated && ['draft', 'inspected'].includes(release.status) && (
        <section className="music-upload">
          <h2>Prepare this release</h2>
          {[...MOODS, 'Cover'].map((mood, i) => (
            <label key={mood}>
              {mood} {release.uploaded.includes(mood.toLowerCase()) ? '✓ uploaded' : ''}
              <input
                type="file"
                disabled={busy}
                accept={i === 3 ? 'image/*' : 'audio/*'}
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  if (file) void action('PUT', `/uploads/${mood.toLowerCase()}`, file);
                }}
              />
            </label>
          ))}
          {release.status === 'draft' && (
            <button
              onClick={() => void action('POST', '/inspect')}
              disabled={
                busy || !['calm', 'building', 'combat'].every((m) => release.uploaded.includes(m))
              }
            >
              Inspect tracks
            </button>
          )}
          {release.inspection && (
            <>
              <p>
                {release.inspection.seconds
                  .map((seconds, i) => `${MOODS[i]}: ${seconds.toFixed(3)}s`)
                  .join(' · ')}
              </p>
              <label>
                Length repair
                <select value={repair} onChange={(e) => setRepair(e.target.value)}>
                  <option value="none">Keep lengths (must already match)</option>
                  <option value="trim">Trim ends to shortest</option>
                  <option value="pad">Pad ends with silence to longest</option>
                </select>
              </label>
              {repair !== 'none' && (
                <p>
                  {release.inspection.seconds
                    .map(
                      (seconds, i, all) =>
                        `${MOODS[i]}: ${Math.abs(seconds - (repair === 'trim' ? Math.min(...all) : Math.max(...all))).toFixed(3)}s ${repair === 'trim' ? 'removed from end' : 'silence added at end'}`,
                    )
                    .join(' · ')}
                </p>
              )}
              <p>Matching lengths does not align beats or harmony.</p>
              <label>
                <input
                  type="checkbox"
                  checked={master}
                  onChange={(e) => setMaster(e.target.checked)}
                />{' '}
                Apply soundtrack loudness targets and peak limiting
              </label>
              <button
                disabled={busy || (!release.inspection.equal && repair === 'none')}
                onClick={() => void action('POST', '/convert', { repair, master })}
              >
                Convert and prepare preview
              </button>
            </>
          )}
        </section>
      )}
      {owner && release.status === 'ready' && (
        <button
          disabled={busy}
          onClick={() =>
            void action(
              'POST',
              '/publish',
              release.generated ? { license: release.metadata.license } : undefined,
            )
          }
        >
          Publish this release
        </button>
      )}
      {owner && release.status !== 'withdrawn' && (
        <button disabled={busy} onClick={() => void action('DELETE', '')}>
          {release.status === 'published'
            ? 'Withdraw release'
            : release.generated
              ? 'Delete private release'
              : 'Cancel upload'}
        </button>
      )}
      {account && release.status === 'published' && (
        <>
          <button
            aria-pressed={release.liked}
            onClick={() => void action(release.liked ? 'DELETE' : 'PUT', '/like')}
          >
            ♥ {release.likes} · {release.liked ? 'Unlike' : 'Like'}
          </button>
          <details>
            <summary>Report this release</summary>
            <label>
              Reason
              <textarea
                value={reason}
                maxLength={2000}
                onChange={(e) => setReason(e.target.value)}
              />
            </label>
            <button
              disabled={!reason.trim()}
              onClick={() => void action('POST', '/report', { reason })}
            >
              Send report
            </button>
          </details>
        </>
      )}
    </main>
  );
}

export function MusicCreate() {
  const { account } = useSession(),
    { navigate } = useRouter();
  const [metadata, setMetadata] = useState<MusicMetadata>({
    title: '',
    artist: '',
    description: '',
    credits: '',
    license: 'CC0-1.0',
    sources: [],
    tags: [],
    aiGenerated: false,
  });
  const [sources, setSources] = useState(''),
    [tags, setTags] = useState(''),
    [notice, setNotice] = useState(''),
    [busy, setBusy] = useState(false);
  if (!account || account.kind !== 'registered')
    return (
      <main>
        <h1>Share music</h1>
        <p>Sign in with a registered account to publish music.</p>
        <a href="/signin">Sign in</a>
      </main>
    );
  return (
    <main className="music-create">
      <h1>Share a soundtrack</h1>
      <p>
        Prepare Calm, Building, and Combat arrangements of the same piece. Each must last 10 seconds
        to 15 minutes. Originals are deleted after conversion; keep your own source files.
      </p>
      <form
        onSubmit={(event) => {
          event.preventDefault();
          setBusy(true);
          void request<MusicRelease>('POST', '/api/v1/music', {
            body: {
              ...metadata,
              sources: sources
                .split('\n')
                .map((s) => s.trim())
                .filter(Boolean),
              tags: tags
                .split(',')
                .map((s) => s.trim())
                .filter(Boolean),
            },
          })
            .then((release) => navigate(`/music/${release.id}`))
            .catch((e) => setNotice(errorText(e)))
            .finally(() => setBusy(false));
        }}
      >
        {(['title', 'artist', 'description', 'credits'] as const).map((key) => (
          <label key={key}>
            {key.charAt(0).toUpperCase() + key.slice(1)}
            <textarea
              required={key === 'title' || key === 'artist'}
              maxLength={key === 'title' || key === 'artist' ? 128 : 4000}
              value={metadata[key]}
              onChange={(e) => setMetadata((old) => ({ ...old, [key]: e.target.value }))}
            />
          </label>
        ))}
        <label>
          Licence
          <select
            value={metadata.license}
            onChange={(e) =>
              setMetadata((old) => ({
                ...old,
                license: e.target.value as MusicMetadata['license'],
              }))
            }
          >
            <option value="CC0-1.0">CC0 1.0</option>
            <option value="CC-BY-4.0">CC BY 4.0</option>
            <option value="CC-BY-SA-4.0">CC BY-SA 4.0</option>
          </select>
        </label>
        <label>
          Source links (one per line)
          <textarea value={sources} onChange={(e) => setSources(e.target.value)} />
        </label>
        <label>
          Tags (comma separated)
          <input value={tags} onChange={(e) => setTags(e.target.value)} />
        </label>
        <label>
          <input
            type="checkbox"
            checked={metadata.aiGenerated}
            onChange={(e) => setMetadata((old) => ({ ...old, aiGenerated: e.target.checked }))}
          />{' '}
          This music includes AI-generated audio
        </label>
        <label>
          <input type="checkbox" required /> I can share these recordings and artwork under the
          selected licence.
        </label>
        <button disabled={busy}>Continue to uploads</button>
        {notice && <p role="alert">{notice}</p>}
      </form>
    </main>
  );
}
