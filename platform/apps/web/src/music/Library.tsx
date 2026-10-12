import { statusLabel } from '../i18n.tsx';
import { displayMessage } from '../i18n.tsx';
import { translateError } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useEffect, useRef, useState } from 'react';
import type { MusicMetadata, MusicRelease } from '@glob2/protocol';
import { request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import { MusicValidation } from './Validation.tsx';
import { MOODS, MusicPlayer } from './Player.tsx';
import { useMusicCatalogue } from './useMusicCatalogue.ts';

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
import { Cover } from './Cover.tsx';
function errorText(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}
function PreviewDialog({ release, close }: { release: MusicRelease; close: () => void }) {
  useLocale();
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
        <button onClick={close}>{t('Close preview')}</button>
      </div>
      <MusicPlayer release={release} />
    </dialog>
  );
}
export function MusicLibrary() {
  useLocale();
  const { account } = useSession();
  const [query, setQuery] = useState(''),
    [sort, setSort] = useState('likes'),
    [licence, setLicence] = useState(''),
    [ai, setAi] = useState(''),
    [tag, setTag] = useState('');
  const [appliedQuery, setAppliedQuery] = useState('');
  const [appliedTag, setAppliedTag] = useState('');
  const applySearch = useLibrarySearch(query, appliedQuery, setAppliedQuery);
  const applyTag = useLibrarySearch(tag, appliedTag, setAppliedTag);
  const [min, setMin] = useState(10),
    [max, setMax] = useState(900),
    [mine, setMine] = useState(false);
  const [selected, setSelected] = useState<Map<string, string>>(new Map());
  const [preview, setPreview] = useState<MusicRelease | null>(null),
    [notice, setNotice] = useState(''),
    [busy, setBusy] = useState(false);
  const filters = new URLSearchParams({
    q: appliedQuery,
    sort,
    license: licence,
    ai,
    tag: appliedTag,
    min: String(min),
    max: String(max),
    mine: mine ? '1' : '0',
  }).toString();
  const { items, setItems, next, loading, error, more, reload } = useMusicCatalogue(filters);
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
  const reset = () => {
    setQuery('');
    setAppliedQuery('');
    setTag('');
    setAppliedTag('');
    setSort('likes');
    setLicence('');
    setAi('');
    setMin(10);
    setMax(900);
  };
  const filtered = Boolean(
    appliedQuery || licence || ai || appliedTag || min !== 10 || max !== 900,
  );
  const activeExtra =
    Number(Boolean(ai)) + Number(Boolean(appliedTag)) + Number(min !== 10) + Number(max !== 900);
  return (
    <section className="music-library library-page">
      <LibraryHeader
        art="inn"
        title={t('Music for your colony')}
        description={t('Three moods. One shared rhythm. Find your next soundtrack.')}
        actions={
          <>
            <Link to="/music/new" className="btn primary">
              {t('Share music')}
            </Link>
            <Link to="/music-studio" className="btn">
              {t('Build in AI Music Studio')}
            </Link>
          </>
        }
      />
      {account && (
        <LibraryNav label={t('Music')}>
          <button className={!mine ? 'on' : ''} aria-pressed={!mine} onClick={() => setMine(false)}>
            {t('Browse')}
          </button>
          <button className={mine ? 'on' : ''} aria-pressed={mine} onClick={() => setMine(true)}>
            {t(' My releases and uploads').trim()}
          </button>
        </LibraryNav>
      )}
      <LibraryFilters
        onSubmit={(event) => {
          event.preventDefault();
          applySearch();
          applyTag();
        }}
        activeExtra={activeExtra}
        extra={
          <>
            <LibraryField label={t('AI disclosure')}>
              <select value={ai} onChange={(e) => setAi(e.target.value)}>
                <option value="">{t('All music')}</option>
                <option value="false">{t('Not AI-generated')}</option>
                <option value="true">{t('AI-generated')}</option>
              </select>
            </LibraryField>
            <LibraryField label={t('Tag')}>
              <input
                value={tag}
                onChange={(e) => setTag(e.target.value)}
                placeholder={t('forest')}
              />
            </LibraryField>
            <LibraryField label={t('Minimum seconds')}>
              <input
                type="number"
                min="10"
                max="900"
                value={min}
                onChange={(e) => setMin(+e.target.value)}
              />
            </LibraryField>
            <LibraryField label={t('Maximum seconds')}>
              <input
                type="number"
                min="10"
                max="900"
                value={max}
                onChange={(e) => setMax(+e.target.value)}
              />
            </LibraryField>
          </>
        }
      >
        <LibraryField label={t('Search')} search>
          <input
            type="search"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder={t('Title, artist, description…')}
          />
        </LibraryField>
        <LibraryField label={t('Sort')}>
          <select value={sort} onChange={(e) => setSort(e.target.value)}>
            <option value="likes">{t('Most liked')}</option>
            <option value="recent">{t('Newest')}</option>
            <option value="downloads">{t('Most downloaded')}</option>
          </select>
        </LibraryField>
        <LibraryField label={t('Licence')}>
          <select value={licence} onChange={(e) => setLicence(e.target.value)}>
            <option value="">{t('All open licences')}</option>
            <option>{t('CC0-1.0')}</option>
            <option>{t('CC-BY-4.0')}</option>
            <option>{t('CC-BY-SA-4.0')}</option>
          </select>
        </LibraryField>
        <button type="button" onClick={reset}>
          {t('Reset filters')}
        </button>
      </LibraryFilters>
      {notice && <p role="alert">{notice}</p>}
      {busy && <p role="status">{t('Loading…')}</p>}
      <LibraryResults
        count={(data) => data.length}
        busy={loading}
        load={
          error
            ? { status: 'error', error: new Error(error) }
            : loading && !items.length
              ? { status: 'loading' }
              : { status: 'ready', data: items }
        }
        retry={reload}
      >
        {(releases) => (
          <>
            <LibraryGrid>
              {releases.map((release) => (
                <LibraryCard key={release.id}>
                  <Link to={`/music/${release.id}`} aria-labelledby={`music-title-${release.id}`}>
                    <Cover release={release} />
                    <h2 id={`music-title-${release.id}`}>{release.metadata.title}</h2>
                  </Link>
                  <div className="music-card-body">
                    <p className="library-metadata">
                      <RichMessage
                        source={'{slot0} · {slot1} seconds'}
                        slots={{
                          slot0: release.metadata.artist,
                          slot1: Math.round(release.frames / 48000),
                        }}
                        singular={'{slot0} · {slot1} second'}
                        count={Number(Math.round(release.frames / 48000))}
                      />
                    </p>
                    <p className="library-description">{release.metadata.description}</p>
                    <p className="library-metadata">
                      {release.metadata.license}
                      {release.metadata.aiGenerated ? t(' · AI-generated') : ''}
                    </p>
                    {release.status !== 'published' && (
                      <p>
                        <RichMessage
                          source={'Status: {slot0}'}
                          slots={{ slot0: statusLabel(release.status) }}
                        />
                      </p>
                    )}
                    <div className="music-card-actions">
                      <button
                        id={`music-preview-${release.id}`}
                        aria-labelledby={`music-preview-${release.id} music-title-${release.id}`}
                        disabled={!release.tracks.length}
                        onClick={(event) => {
                          event.currentTarget.focus();
                          setPreview(release);
                        }}
                      >
                        {t('Preview')}
                      </button>
                      <button
                        disabled={account?.kind !== 'registered' || release.status !== 'published'}
                        aria-label={t('{value0} {value1} · {value2} likes', {
                          value0: release.liked ? 'Unlike' : 'Like',
                          value1: release.metadata.title,
                          value2: release.likes,
                        })}
                        title={
                          account?.kind !== 'registered'
                            ? t('Sign in with a registered account to like music')
                            : undefined
                        }
                        aria-pressed={release.liked}
                        onClick={() => void like(release)}
                      >
                        <RichMessage source={'♥ {slot0}'} slots={{ slot0: release.likes }} />
                      </button>
                      {release.tracks.length > 0 && (
                        <a
                          id={`music-download-${release.id}`}
                          aria-labelledby={`music-download-${release.id} music-title-${release.id}`}
                          href={`/api/v1/music/${release.id}/download`}
                          download
                        >
                          {t('Download ZIP')}
                        </a>
                      )}
                    </div>
                    {release.status === 'published' && (
                      <label className="library-checkbox">
                        <input
                          type="checkbox"
                          aria-labelledby={`music-select-${release.id} music-title-${release.id}`}
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
                        <span id={`music-select-${release.id}`}>{t('Select for download')}</span>
                      </label>
                    )}
                  </div>
                </LibraryCard>
              ))}
            </LibraryGrid>
            {!busy && !loading && !items.length && (
              <LibraryEmpty
                art="inn"
                action={
                  filtered ? (
                    <button onClick={reset}>{t('Clear filters')}</button>
                  ) : (
                    <Link className="btn primary" to="/music/new">
                      {t('Share music')}
                    </Link>
                  )
                }
              >
                {filtered
                  ? t('No results match these filters.')
                  : t('No music found. Try different filters, or share the first set.')}
              </LibraryEmpty>
            )}
          </>
        )}
      </LibraryResults>
      {next && (
        <button onClick={() => void more()} disabled={busy || loading}>
          {t('Load more')}
        </button>
      )}
      {selected.size > 0 && (
        <aside className="music-selection" aria-label={t('Selected music')}>
          <span>
            <RichMessage
              source={'{slot0} selected · up to 10 sets, 64 MiB'}
              slots={{ slot0: selected.size }}
            />
          </span>
          <button onClick={() => void downloadSelected()} disabled={busy}>
            {t('Download selected')}
          </button>
          <button onClick={() => setSelected(new Map())}>{t('Clear selection')}</button>
        </aside>
      )}
      {preview && (
        <PreviewDialog key={preview.id} release={preview} close={() => setPreview(null)} />
      )}
    </section>
  );
}

export function MusicDetail({ id }: { id: string }) {
  useLocale();
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
  if (data.status === 'loading') return <p role="status">{t('Loading music…')}</p>;
  if (data.status === 'error') return <p role="alert">{translateError(data.error)}</p>;
  const release = data.data,
    owner = account?.id === release.ownerId;
  return (
    <main className="music-detail">
      <Link to="/music">{t('← Music library')}</Link>
      <section className="music-listening-room">
        <div className="music-detail-heading">
          <Cover release={release} />
          <div>
            <span className="music-eyebrow">{t('A SOUNDTRACK FOR YOUR COLONY')}</span>
            <h1>{release.metadata.title}</h1>
            <p className="music-artist">
              <RichMessage source={'By {slot0}'} slots={{ slot0: release.metadata.artist }} />
            </p>
            <p className="music-description-full">{release.metadata.description}</p>
            <p className="music-attribution">
              <RichMessage
                source={'{slot0} · {slot1}'}
                slots={{
                  slot0: release.metadata.license,
                  slot1: release.metadata.aiGenerated ? t('AI-generated') : t('Not AI-generated'),
                }}
              />
            </p>
          </div>
        </div>
        {notice && <p role="status">{displayMessage(notice)}</p>}
        {release.error && <p role="alert">{release.error}</p>}
        {release.status !== 'published' && (
          <p className="music-release-state">
            <RichMessage
              source={'Status: {slot0}'}
              slots={{ slot0: statusLabel(release.status) }}
            />
          </p>
        )}
        {release.tracks.length === 3 && (
          <>
            <MusicPlayer key={release.id} release={release} />
            <div className="music-downloads">
              <a className="btn" href={`/api/v1/music/${id}/download`} download>
                {t('Download set')}
              </a>
              <details>
                <summary>{t('Individual moods')}</summary>
                <div className="music-card-actions">
                  {release.tracks.map((track, i) => (
                    <a key={track.mood} href={track.url} download={`a${i + 1}.opus`}>
                      <RichMessage source={'Download {slot0}'} slots={{ slot0: t(MOODS[i]) }} />
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
        <summary>{t('Credits & sources')}</summary>
        <p>{release.metadata.credits}</p>
        <p>
          <RichMessage source={'License: {slot0}'} slots={{ slot0: release.metadata.license }} />
        </p>
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
          <h2>{t('Prepare this release')}</h2>
          {[...MOODS, t('Cover')].map((mood, i) => (
            <label key={mood}>
              {mood} {release.uploaded.includes(mood.toLowerCase()) ? t('✓ uploaded') : ''}
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
              {t('Inspect tracks')}
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
                {t('Length repair')}
                <select value={repair} onChange={(e) => setRepair(e.target.value)}>
                  <option value="none">{t('Keep lengths (must already match)')}</option>
                  <option value="trim">{t('Trim ends to shortest')}</option>
                  <option value="pad">{t('Pad ends with silence to longest')}</option>
                </select>
              </label>
              {repair !== 'none' && (
                <p>
                  {release.inspection.seconds
                    .map(
                      (seconds, i, all) =>
                        `${MOODS[i]}: ${Math.abs(seconds - (repair === 'trim' ? Math.min(...all) : Math.max(...all))).toFixed(3)}s ${repair === 'trim' ? t('removed from end') : t('silence added at end')}`,
                    )
                    .join(' · ')}
                </p>
              )}
              <p>{t('Matching lengths does not align beats or harmony.')}</p>
              <label>
                <input
                  type="checkbox"
                  checked={master}
                  onChange={(e) => setMaster(e.target.checked)}
                />{' '}
                {t('Apply soundtrack loudness targets and peak limiting')}
              </label>
              <button
                disabled={busy || (!release.inspection.equal && repair === 'none')}
                onClick={() => void action('POST', '/convert', { repair, master })}
              >
                {t('Convert and prepare preview')}
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
          {t('Publish this release')}
        </button>
      )}
      {owner && release.status !== 'withdrawn' && (
        <button disabled={busy} onClick={() => void action('DELETE', '')}>
          {release.status === 'published'
            ? t('Withdraw release')
            : release.generated
              ? t('Delete private release')
              : t('Cancel upload')}
        </button>
      )}
      {account && release.status === 'published' && (
        <>
          <button
            aria-pressed={release.liked}
            onClick={() => void action(release.liked ? 'DELETE' : 'PUT', '/like')}
          >
            <RichMessage
              source={'♥ {slot0} · {slot1}'}
              slots={{ slot0: release.likes, slot1: release.liked ? t('Unlike') : t('Like') }}
            />
          </button>
          <details>
            <summary>{t('Report this release')}</summary>
            <label>
              {t('Reason')}
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
              {t('Send report')}
            </button>
          </details>
        </>
      )}
    </main>
  );
}

export function MusicCreate() {
  useLocale();
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
        <h1>{t('Share music')}</h1>
        <p>{t('Sign in with a registered account to publish music.')}</p>
        <a href="/signin">{t('Sign in')}</a>
      </main>
    );
  return (
    <main className="music-create">
      <h1>{t('Share a soundtrack')}</h1>
      <p>
        {t(
          'Prepare Calm, Building, and Combat arrangements of the same piece. Each must last 10 seconds to 15 minutes. Originals are deleted after conversion; keep your own source files.',
        )}
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
          {t('Licence')}
          <select
            value={metadata.license}
            onChange={(e) =>
              setMetadata((old) => ({
                ...old,
                license: e.target.value as MusicMetadata['license'],
              }))
            }
          >
            <option value="CC0-1.0">{t('CC0 1.0')}</option>
            <option value="CC-BY-4.0">{t('CC BY 4.0')}</option>
            <option value="CC-BY-SA-4.0">{t('CC BY-SA 4.0')}</option>
          </select>
        </label>
        <label>
          {t('Source links (one per line)')}
          <textarea value={sources} onChange={(e) => setSources(e.target.value)} />
        </label>
        <label>
          {t('Tags (comma separated)')}
          <input value={tags} onChange={(e) => setTags(e.target.value)} />
        </label>
        <label>
          <input
            type="checkbox"
            checked={metadata.aiGenerated}
            onChange={(e) => setMetadata((old) => ({ ...old, aiGenerated: e.target.checked }))}
          />{' '}
          {t('This music includes AI-generated audio')}
        </label>
        <label>
          <input type="checkbox" required />{' '}
          {t(' I can share these recordings and artwork under the selected licence.')}
        </label>
        <button disabled={busy}>{t('Continue to uploads')}</button>
        {notice && <p role="alert">{displayMessage(notice)}</p>}
      </form>
    </main>
  );
}
