import { useEffect, useRef, useState, type FormEvent } from 'react';
import {
  AI_TAGS,
  AI_CHECK_LABELS,
  pendingAiReport,
  passedAiReport,
  type AiDetail,
  type AiUpload,
  type AiValidationReport,
  type PublishAiRequest,
} from '@glob2/protocol';
import { aiApi } from '../aiApi.ts';
import { aiBrowseUrl, aiReturnUrl, readAiBrowse, type AiBrowseState } from '../aiBrowse.ts';
import { ApiError } from '../api.ts';
import { Loaded, ErrorNotice, Empty } from '../components/common.tsx';
import { GameArt } from '../art.tsx';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import { date } from '../format.ts';
import '../styles/ais.css';
const DOCS = 'https://github.com/Globulation2/glob2/blob/master/docs/development/';
const STARTER = 'https://github.com/Globulation2/glob2-javascript-ai-starter-exampler';

function Learn() {
  const toggle = useRef<HTMLButtonElement>(null);
  const [open, setOpen] = useState(false);
  const [dismissed, setDismissed] = useState(() => {
    try {
      return localStorage.getItem('ai-introduction') === 'dismissed';
    } catch {
      return false;
    }
  });
  const dismiss = () => {
    setOpen(false);
    setDismissed(true);
    toggle.current?.focus();
    try {
      localStorage.setItem('ai-introduction', 'dismissed');
    } catch {
      /* Storage may be unavailable. */
    }
  };
  return (
    <section className="ai-learn">
      <button ref={toggle} className="small" aria-expanded={open} onClick={() => setOpen(!open)}>
        How AIs work <span aria-hidden="true">{open ? '−' : '+'}</span>
      </button>
      {!open && !dismissed && (
        <div className="ai-learn-summary">
          <p>
            Install community AIs for local games. Versions work offline. Only run code from authors
            you trust.
          </p>
          <button className="small" onClick={dismiss}>
            Got it
          </button>
        </div>
      )}
      {open && (
        <div className="ai-learn-body">
          <div>
            <h2>A new mind for your colony</h2>
            <p>
              Community-made JavaScript players control a colony through Glob2’s AI API. Download a
              version, import it in Settings → Custom AIs, then choose it for a computer seat in a
              local game. Installed versions work offline; saved games keep their original code.
            </p>
            <p className="caption">
              Compatibility checks exercise short games and save/resume. They do not certify safety
              or playing strength. Only run code from authors you trust.
            </p>
          </div>
          <nav aria-label="AI authoring resources">
            <a href={DOCS + 'javascript.md'}>Authoring guide ↗</a>
            <a href={DOCS + 'javascript-api.md'}>JavaScript API reference ↗</a>
            <a href={STARTER}>Start building an AI ↗</a>
            <button className="small" onClick={dismiss}>
              Got it
            </button>
          </nav>
        </div>
      )}
    </section>
  );
}
function Tags({ value, onChange }: { value: string[]; onChange: (tags: string[]) => void }) {
  return (
    <div className="ai-tags" role="group" aria-label="AI tags">
      {AI_TAGS.map((tag) => (
        <button
          type="button"
          key={tag}
          className="small"
          aria-pressed={value.includes(tag)}
          disabled={!value.includes(tag) && value.length >= 5}
          onClick={() =>
            onChange(
              value.includes(tag)
                ? value.filter((t) => t !== tag)
                : value.length < 5
                  ? [...value, tag]
                  : value,
            )
          }
        >
          {tag}
        </button>
      ))}
    </div>
  );
}
export function AiChecklist({ report }: { report: AiValidationReport }) {
  return (
    <ol className="ai-checklist" aria-label="Compatibility checks">
      {report.checks.map((c) => (
        <li key={c.id} data-status={c.status}>
          <span className={c.status === 'running' ? 'ai-spin' : ''} aria-hidden="true">
            {{ passed: '✓', failed: '×', skipped: '—', pending: '○', running: '◌' }[c.status]}
          </span>
          <div>
            <strong>{AI_CHECK_LABELS[c.id]}</strong>
            <small>
              {c.status}
              {c.message ? ': ' + c.message : ''}
            </small>
          </div>
        </li>
      ))}
    </ol>
  );
}
export function Ais({ view = 'discover' }: { view?: 'discover' | 'mine' | 'favourites' }) {
  const { account } = useSession();
  const { location, navigate } = useRouter();
  const browse = readAiBrowse(location.search);
  const { query, tags, sort, pages } = browse;
  const queryKey = location.path + '?' + query;
  const [searchDraft, setSearchDraft] = useState({ key: queryKey, value: query });
  const search = searchDraft.key === queryKey ? searchDraft.value : query;
  const setSearch = (value: string) => setSearchDraft({ key: queryKey, value });
  const restoredFocus = useRef('');
  const updateBrowse = (update: Partial<AiBrowseState>) =>
    navigate(aiBrowseUrl(location.path, { ...browse, focus: '', cursor: '', ...update }), {
      replace: true,
    });
  useEffect(() => {
    if (search.trim() === query) return;
    const timer = setTimeout(() => updateBrowse({ query: search.trim(), pages: 1 }), 250);
    return () => clearTimeout(timer);
    // URL changes are authoritative; typing is debounced without adding history entries.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [search, query, location.path, location.search.toString()]);
  const load = useLoad(
    async (signal) => {
      let cursor: string | undefined = browse.cursor || undefined;
      const items: Awaited<ReturnType<typeof aiApi.list>>['items'] = [];
      for (let i = 0; i < pages; i++) {
        const page = await aiApi.list(
          {
            q: query,
            tags: tags.join(','),
            sort,
            owner: view === 'mine' ? 'me' : undefined,
            favourites: view === 'favourites' ? 'true' : undefined,
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
    [query, tags.join(','), sort, pages, browse.cursor, view, account?.id],
  );
  useEffect(() => {
    if (load.status !== 'ready' || !browse.focus || restoredFocus.current === browse.focus) return;
    const card = document.getElementById('ai-card-' + browse.focus);
    if (card) {
      card.focus();
      restoredFocus.current = browse.focus;
    }
  }, [load.status, browse.focus]);
  return (
    <div className="ai-library">
      <header className="page-head">
        <GameArt name="swarm" size={80} />
        <div className="grow">
          <p className="caption">COMMUNITY · LOCAL PLAY</p>
          <h1>AI Library</h1>
          <p className="sub">Discover a new opponent. Find a new way to play.</p>
        </div>
        <Link className="btn primary" to="/ais/new">
          Share your AI
        </Link>
      </header>
      <nav className="seg" aria-label="AI library views">
        {(
          [
            ['discover', '/ais', 'Discover'],
            ['favourites', '/ais/favourites', 'Favourites'],
            ['mine', '/ais/mine', 'My AIs'],
          ] as const
        ).map(([id, to, label]) => (
          <Link
            key={id}
            to={to}
            className={id === view ? 'on' : ''}
            aria-current={id === view ? 'page' : undefined}
          >
            {label}
          </Link>
        ))}
      </nav>
      <Learn />
      {!account && view !== 'discover' ? (
        <div className="notice">
          <a href="/signin">Sign in</a> to see your library.
        </div>
      ) : (
        <>
          <form
            className="filters"
            role="search"
            onSubmit={(e) => {
              e.preventDefault();
              updateBrowse({ query: search.trim(), pages: 1 });
            }}
          >
            <input
              type="search"
              maxLength={128}
              placeholder="Search names, descriptions, authors…"
              aria-label="Search AIs"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
            />
            <select
              aria-label="Sort AIs"
              value={sort}
              onChange={(e) => {
                updateBrowse({ sort: e.target.value, pages: 1 });
              }}
            >
              <option value="likes">Most liked</option>
              <option value="newest">Newest</option>
              <option value="updated">Recently updated</option>
              <option value="downloads">Most downloaded</option>
            </select>
            <button
              type="button"
              onClick={() => {
                setSearch('');
                updateBrowse({ query: '', tags: [], sort: 'likes', pages: 1 });
              }}
            >
              Reset filters
            </button>
          </form>
          <Tags
            value={tags}
            onChange={(v) => {
              updateBrowse({ tags: v, pages: 1 });
            }}
          />
          {load.status === 'error' && <button onClick={load.reload}>Try again</button>}
          <Loaded load={load}>
            {(data) =>
              data.items.length ? (
                <>
                  <div className="ai-grid">
                    {data.items.map((ai) => (
                      <Link
                        className="ai-card card"
                        id={'ai-card-' + ai.id}
                        key={ai.id}
                        to={
                          '/ais/' +
                          ai.id +
                          '?return=' +
                          encodeURIComponent(
                            aiBrowseUrl(location.path, { ...browse, focus: ai.id }),
                          )
                        }
                      >
                        <div className="ai-card-top">
                          <span className="ai-monogram" aria-hidden="true">
                            {ai.name.slice(0, 2).toUpperCase()}
                          </span>
                          <span className="badge">v{ai.latestVersion.label}</span>
                        </div>
                        <h2>{ai.name}</h2>
                        <p className="caption">
                          by {ai.owner.displayName}
                          {ai.hidden ? ' · hidden' : ''}
                        </p>
                        <p className="ai-excerpt">
                          {ai.description || 'A community-created colony controller.'}
                        </p>
                        <div className="ai-tags">
                          {ai.tags.map((t) => (
                            <span className="badge" key={t}>
                              {t}
                            </span>
                          ))}
                        </div>
                        <footer>
                          <span>♥ {ai.likes} likes</span>
                          <span>↓ {ai.downloads} downloads</span>
                          {ai.favourited && <span aria-label="Favourited">★</span>}
                        </footer>
                      </Link>
                    ))}
                  </div>
                  {data.cursor && (
                    <button
                      onClick={() =>
                        pages < 20
                          ? updateBrowse({ pages: pages + 1, cursor: browse.cursor })
                          : navigate(
                              aiBrowseUrl(location.path, {
                                ...browse,
                                pages: 1,
                                cursor: data.cursor ?? '',
                                focus: '',
                              }),
                            )
                      }
                    >
                      {pages < 20 ? 'Show more' : 'Next results'}
                    </button>
                  )}
                </>
              ) : (
                <Empty art="swarm">
                  {query || tags.length
                    ? 'No AIs match these filters.'
                    : view === 'mine'
                      ? 'Your first AI belongs here.'
                      : view === 'favourites'
                        ? 'Favourite an AI to keep it close.'
                        : 'A new library is taking shape. Share the first AI.'}
                </Empty>
              )
            }
          </Loaded>
        </>
      )}
    </div>
  );
}
function AiDetails({ detail, reload }: { detail: AiDetail; reload: () => void }) {
  const { account } = useSession(),
    { navigate, location } = useRouter();
  const returnUrl = aiReturnUrl(location.search.get('return'));
  const editButton = useRef<HTMLButtonElement>(null);
  const reportButton = useRef<HTMLButtonElement>(null);
  const ai = detail.ai;
  const [selected, setSelected] = useState(detail.versions[0]?.id ?? ''),
    [error, setError] = useState<Error>(),
    [busy, setBusy] = useState(false),
    [editing, setEditing] = useState(false),
    [reporting, setReporting] = useState(false),
    [reason, setReason] = useState('broken'),
    [reportText, setReportText] = useState('');
  const [name, setName] = useState(ai.name),
    [description, setDescription] = useState(ai.description),
    [tags, setTags] = useState<string[]>(ai.tags),
    [visibility, setVisibility] = useState(ai.visibility);
  const v = detail.versions.find((v) => v.id === selected) ?? detail.versions[0];
  const run = async (fn: () => Promise<unknown>) => {
    setBusy(true);
    setError(undefined);
    try {
      await fn();
      reload();
    } catch (e) {
      setError(e as Error);
    } finally {
      setBusy(false);
    }
  };
  return (
    <>
      <Link to={returnUrl}>
        ←{' '}
        {returnUrl.startsWith('/ais/favourites')
          ? 'Favourites'
          : returnUrl.startsWith('/ais/mine')
            ? 'My AIs'
            : 'AI Library'}
      </Link>
      <header className="page-head">
        <span className="ai-monogram" aria-hidden="true">
          {ai.name.slice(0, 2).toUpperCase()}
        </span>
        <div className="grow">
          <h1>{ai.name}</h1>
          <p className="sub">
            by {ai.owner.displayName} · updated {date(ai.updatedAt)}
          </p>
        </div>
        {detail.viewer.owner && (
          <Link className="btn primary" to={'/ais/' + ai.id + '/new'}>
            Publish a version
          </Link>
        )}
      </header>
      {ai.hidden && <div className="notice warn">Hidden by a moderator: {ai.hiddenReason}</div>}
      <div className="ai-detail-grid">
        <section className="card">
          <div className="ai-tags">
            {ai.tags.map((t) => (
              <span className="badge" key={t}>
                {t}
              </span>
            ))}
          </div>
          <p className="ai-description">{ai.description}</p>
          <div className="tiles">
            <div className="tile">
              <span className="caption">Likes</span>
              <div className="v">{ai.likes}</div>
            </div>
            <div className="tile">
              <span className="caption">All downloads</span>
              <div className="v">{ai.downloads}</div>
            </div>
          </div>
          <div className="toolbar">
            {account?.kind === 'registered' ? (
              <>
                <button
                  disabled={busy}
                  aria-pressed={ai.liked}
                  onClick={() => void run(() => aiApi.social(ai.id, 'like', !ai.liked))}
                >
                  {ai.liked ? '♥ Liked' : '♡ Like'}
                </button>
                <button
                  disabled={busy}
                  aria-pressed={ai.favourited}
                  onClick={() => void run(() => aiApi.social(ai.id, 'favourite', !ai.favourited))}
                >
                  {ai.favourited ? '★ Favourited' : '☆ Favourite'}
                </button>
              </>
            ) : (
              <a className="btn" href="/signin">
                Sign in to like or favourite
              </a>
            )}
            {account && (
              <button
                ref={reportButton}
                aria-expanded={reporting}
                aria-controls="ai-report"
                onClick={() => setReporting(!reporting)}
              >
                Report
              </button>
            )}
            {detail.viewer.owner && (
              <button
                ref={editButton}
                aria-expanded={editing}
                aria-controls="ai-edit"
                onClick={() => setEditing(!editing)}
              >
                Edit details
              </button>
            )}
            {detail.viewer.moderator && (
              <button
                disabled={busy}
                onClick={() => {
                  const reason = ai.hidden ? '' : window.prompt('Reason for hiding this AI');
                  if (reason !== null) void run(() => aiApi.hide(ai.id, !ai.hidden, reason));
                }}
              >
                {ai.hidden ? 'Unhide' : 'Hide'}
              </button>
            )}
          </div>
          {reporting && (
            <form
              id="ai-report"
              aria-label="Report AI"
              onSubmit={(e) => {
                e.preventDefault();
                void run(async () => {
                  await aiApi.report(ai.id, reason, reportText);
                  setReporting(false);
                  reportButton.current?.focus();
                });
              }}
            >
              <label className="field">
                Reason
                <select autoFocus value={reason} onChange={(e) => setReason(e.target.value)}>
                  {['broken', 'offensive', 'copyright', 'other'].map((r) => (
                    <option key={r}>{r}</option>
                  ))}
                </select>
              </label>
              <label className="field">
                Details
                <textarea
                  maxLength={2000}
                  value={reportText}
                  onChange={(e) => setReportText(e.target.value)}
                />
              </label>
              <div className="toolbar">
                <button disabled={busy}>Send report</button>
                <button
                  type="button"
                  disabled={busy}
                  onClick={() => {
                    setReporting(false);
                    reportButton.current?.focus();
                  }}
                >
                  Cancel report
                </button>
              </div>
            </form>
          )}
          {editing && (
            <form
              id="ai-edit"
              aria-label="Edit AI details"
              onSubmit={(e) => {
                e.preventDefault();
                void run(async () => {
                  await aiApi.update(ai.id, {
                    name,
                    description,
                    tags: tags as PublishAiRequest['tags'],
                    visibility,
                  });
                  setEditing(false);
                  editButton.current?.focus();
                });
              }}
            >
              <label className="field">
                Name
                <input
                  autoFocus
                  required
                  maxLength={128}
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                />
              </label>
              <label className="field">
                Description
                <textarea
                  maxLength={4000}
                  value={description}
                  onChange={(e) => setDescription(e.target.value)}
                />
              </label>
              <Tags value={tags} onChange={setTags} />
              <label className="field">
                Visibility
                <select
                  value={visibility}
                  onChange={(e) => setVisibility(e.target.value as typeof visibility)}
                >
                  {['public', 'unlisted', 'private'].map((x) => (
                    <option key={x}>{x}</option>
                  ))}
                </select>
              </label>
              <div className="toolbar">
                <button className="primary" disabled={busy}>
                  Save details
                </button>
                <button
                  type="button"
                  disabled={busy}
                  onClick={() => {
                    setEditing(false);
                    setName(ai.name);
                    setDescription(ai.description);
                    setTags(ai.tags);
                    setVisibility(ai.visibility);
                    editButton.current?.focus();
                  }}
                >
                  Cancel editing
                </button>
                <button
                  type="button"
                  className="danger"
                  disabled={busy}
                  onClick={() => {
                    if (window.confirm('Delete this AI and its published versions?'))
                      void run(async () => {
                        await aiApi.remove(ai.id);
                        navigate('/ais/mine');
                      });
                  }}
                >
                  Delete AI
                </button>
              </div>
            </form>
          )}
        </section>
        {v && (
          <section className="card">
            <label className="field">
              Version
              <select value={v.id} onChange={(e) => setSelected(e.target.value)}>
                {detail.versions.map((v) => (
                  <option key={v.id} value={v.id}>
                    {v.label} · {date(v.createdAt)}
                  </option>
                ))}
              </select>
            </label>
            <p>{v.notes || 'No release notes.'}</p>
            <p className="caption">
              Profile {v.profile} · {v.downloads} downloads of this version
            </p>
            <a className="btn primary" href={v.downloadUrl} download>
              Download JavaScript (.js)
            </a>
            <p className="caption">Import in Settings → Custom AIs to use in local games.</p>
            {v.validations.map((report) => (
              <details key={report.simVersion + '-' + report.suite} className="ai-validation">
                <summary>
                  {passedAiReport(report)
                    ? '✓ Passed compatibility checks'
                    : '× Compatibility checks failed'}
                </summary>
                <p className="caption ai-hash">
                  Engine {report.simVersion} · suite {report.suite}
                </p>
                <AiChecklist report={report} />
              </details>
            ))}
            <details className="ai-validation">
              <summary>Source identity</summary>
              <code className="ai-hash">{v.hash}</code>
            </details>
          </section>
        )}
      </div>
      {error && <ErrorNotice error={error} />}
      <Learn />
    </>
  );
}
export function AiPage({ id }: { id: string }) {
  const load = useLoad((signal) => aiApi.detail(id, signal), [id]);
  return (
    <>
      <Loaded load={load} page="AI">
        {(detail) => <AiDetails key={id} detail={detail} reload={load.reload} />}
      </Loaded>
      {load.status === 'error' && <button onClick={load.reload}>Try again</button>}
    </>
  );
}

export function AiPublish({ id }: { id?: string }) {
  const { account } = useSession(),
    { navigate } = useRouter();
  const [name, setName] = useState(''),
    [description, setDescription] = useState(''),
    [version, setVersion] = useState(''),
    [notes, setNotes] = useState(''),
    [tags, setTags] = useState<string[]>([]),
    [visibility, setVisibility] = useState<PublishAiRequest['visibility']>('public');
  const [upload, setUpload] = useState<AiUpload>(),
    [error, setError] = useState<Error>(),
    [busy, setBusy] = useState(false),
    [checking, setChecking] = useState(false),
    [file, setFile] = useState<File>(),
    [fileFailure, setFileFailure] = useState('');
  const generation = useRef(0),
    controller = useRef<AbortController | undefined>(undefined);
  useEffect(
    () => () => {
      generation.current++;
      controller.current?.abort();
    },
    [],
  );
  useEffect(() => {
    if (!id) return;
    const c = new AbortController();
    void aiApi.detail(id, c.signal).then(
      (d) => {
        setName(d.ai.name);
        setDescription(d.ai.description);
        setTags(d.ai.tags);
        setVisibility(d.ai.visibility);
      },
      (e) => {
        if (!c.signal.aborted) setError(e as Error);
      },
    );
    return () => c.abort();
  }, [id]);
  const check = async (selected: File) => {
    controller.current?.abort();
    const c = new AbortController();
    controller.current = c;
    const token = ++generation.current;
    setUpload(undefined);
    setError(undefined);
    setChecking(true);
    setFile(selected);
    setFileFailure('');
    let submitted = false;
    try {
      if (!selected.name.toLowerCase().endsWith('.js')) throw Error('Choose one bundled .js file.');
      if (!selected.size) throw Error('Choose a nonempty JavaScript file.');
      if (selected.size > 128 * 1024) throw Error('The file must be no larger than 128 KiB.');
      submitted = true;
      let u = await aiApi.upload(selected, c.signal);
      while (token === generation.current) {
        setUpload(u);
        if (u.status !== 'pending') break;
        await new Promise((resolve) => setTimeout(resolve, 1500));
        if (c.signal.aborted) return;
        u = await aiApi.check(u.id, c.signal);
      }
      if (token === generation.current && u.report.metadata) {
        const m = u.report.metadata;
        setName((n) => n || m.name || selected.name.replace(/\.js$/i, ''));
        setDescription((d) => d || m.description);
        setVersion((v) => v || m.version || '1.0.0');
      }
    } catch (e) {
      if (!c.signal.aborted) {
        setError(e as Error);
        if (!submitted || (e instanceof ApiError && [400, 413].includes(e.status)))
          setFileFailure((e as Error).message);
      }
    } finally {
      if (token === generation.current) setChecking(false);
    }
  };
  const publish = async (e: FormEvent) => {
    e.preventDefault();
    if (!upload || upload.status !== 'valid') return;
    setBusy(true);
    setError(undefined);
    try {
      const ai = await aiApi.publish(
        {
          uploadId: upload.id,
          name,
          description,
          version,
          notes,
          tags: tags as PublishAiRequest['tags'],
          visibility,
        },
        id,
      );
      navigate('/ais/' + ai.id);
    } catch (e) {
      setError(e as Error);
    } finally {
      setBusy(false);
    }
  };
  const pending = pendingAiReport('0'.repeat(64), '');
  const report: AiValidationReport = upload?.report ?? {
    ...pending,
    checks: pending.checks.map((c) =>
      fileFailure
        ? {
            ...c,
            status: c.id === 'file' ? 'failed' : 'skipped',
            ...(c.id === 'file' ? { message: fileFailure } : {}),
          }
        : c,
    ),
  };
  const runningCheck = report.checks.find((check) => check.status === 'running');
  const passedChecks = report.checks.filter((check) => check.status === 'passed').length;
  return (
    <>
      <header className="page-head">
        <div>
          <h1>{id ? 'Publish a new version' : 'Share your AI'}</h1>
          <p className="sub">
            Give your colony controller a home. Every release keeps its own source and download
            history.
          </p>
        </div>
      </header>
      {!account ? (
        <div className="notice">
          <a href="/signin">Sign in</a> to share an AI.
        </div>
      ) : (
        <form className="ai-publish-grid" onSubmit={(e) => void publish(e)}>
          <section className="card">
            <label className="ai-drop field">
              Bundled JavaScript file
              <input
                type="file"
                accept=".js"
                disabled={busy}
                onChange={(e) => {
                  const f = e.target.files?.[0];
                  if (f) void check(f);
                }}
              />
              <span className="caption">One .js file · up to 128 KiB · profiles 1 and 2</span>
            </label>
            {!id && (
              <>
                <label className="field">
                  Name
                  <input
                    required
                    maxLength={128}
                    value={name}
                    onChange={(e) => setName(e.target.value)}
                  />
                </label>
                <label className="field">
                  Description
                  <textarea
                    maxLength={4000}
                    rows={5}
                    value={description}
                    onChange={(e) => setDescription(e.target.value)}
                  />
                </label>
                <p className="caption">
                  Choose up to five tags. These describe your strategy, not a tested strength
                  rating.
                </p>
                <Tags value={tags} onChange={setTags} />
                <label className="field">
                  Visibility
                  <select
                    value={visibility}
                    onChange={(e) => setVisibility(e.target.value as typeof visibility)}
                  >
                    <option value="public">Public — listed for everyone</option>
                    <option value="unlisted">Unlisted — accessible by link</option>
                    <option value="private">Private — only you</option>
                  </select>
                </label>
              </>
            )}
            <label className="field">
              Version label
              <input
                required
                maxLength={64}
                placeholder="1.0.0"
                value={version}
                onChange={(e) => setVersion(e.target.value)}
              />
            </label>
            <label className="field">
              Release notes
              <textarea maxLength={2000} value={notes} onChange={(e) => setNotes(e.target.value)} />
            </label>
            <button
              className="primary"
              disabled={busy || checking || upload?.status !== 'valid' || !passedAiReport(report)}
            >
              {busy ? 'Publishing…' : 'Publish'}
            </button>
            <p className="caption">
              Publishing preserves these exact source bytes. Changes require a new version.
            </p>
          </section>
          <aside className="card ai-validation-panel">
            <h2>Compatibility checklist</h2>
            <p role="status">
              {checking
                ? `Checking your AI… ${passedChecks} of ${report.checks.length} checks passed.${runningCheck ? ' Running: ' + AI_CHECK_LABELS[runningCheck.id] + '.' : ''}`
                : upload?.status === 'valid'
                  ? 'Ready to publish'
                  : upload?.status === 'invalid'
                    ? 'Fix the failed checks and choose your updated file.'
                    : upload?.status === 'error'
                      ? 'Validation was interrupted. Your AI has not been published.'
                      : fileFailure
                        ? 'Choose an updated file to try again.'
                        : 'Choose a file to begin.'}
            </p>
            <AiChecklist report={report} />
            {upload?.error && <p className="notice error">{upload.error}</p>}
            {file && !checking && (
              <button type="button" onClick={() => void check(file)}>
                Check again
              </button>
            )}
            <p className="caption">
              Short seeded games check runtime behaviour, repeatability, and save/resume. Passing
              does not certify safety or playing strength.
            </p>
            {error && <ErrorNotice error={error} />}
          </aside>
        </form>
      )}
      <Learn />
    </>
  );
}
