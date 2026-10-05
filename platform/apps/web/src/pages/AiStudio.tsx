import { lazy, Suspense, useCallback, useEffect, useRef, useState } from 'react';
import type {
  AiStudioAccount,
  AiStudioCommand,
  AiStudioProject,
  AiStudioRevision,
  AiValidationReport,
  AiInfo,
} from '@glob2/protocol';
import { ApiError, request } from '../api.ts';
import { checkoutAttempt, checkoutKey } from './aiStudio/checkoutAttempt.ts';
import { useProjectDraft } from './aiStudio/useProjectDraft.ts';
import { aiApi } from '../aiApi.ts';
import { useSession } from '../state.tsx';
import { Link, useRouter } from '../router.tsx';
import { AiChecklist } from './Ais.tsx';
import Playtest, { type Run } from './aiStudio/Playtest.tsx';
import '../styles/ai-studio.css';
const Editor = lazy(() => import('./aiStudio/Editor.tsx'));
const ROOT = '/api/v1/ai-studio';
interface Check {
  report: AiValidationReport;
  status: string;
  error: string | null;
  upload_id: string | null;
  expires_at: string | null;
}
function download(source: string) {
  const url = URL.createObjectURL(new Blob([source], { type: 'application/javascript' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = 'ai.js';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export function AiStudio({ id }: { id?: string }) {
  const { account } = useSession(),
    { location, navigate } = useRouter();
  const [wallet, setWallet] = useState<AiStudioAccount>(),
    [projects, setProjects] = useState<AiStudioProject[]>([]);
  const [prompt, setPrompt] = useState(''),
    [title, setTitle] = useState('My Colony'),
    [error, setError] = useState(''),
    [busy, setBusy] = useState(false);
  const [tab, setTab] = useState<'code' | 'changes' | 'playtest'>('code'),
    [split, setSplit] = useState(36),
    [selected, setSelected] = useState(0),
    [baseline, setBaseline] = useState(''),
    [budget, setBudget] = useState(0),
    [checks, setChecks] = useState<Check[]>([]),
    [run, setRun] = useState<Run>(),
    [seed, setSeed] = useState(19),
    [opponent, setOpponent] = useState('numbi'),
    [runResult, setRunResult] = useState(''),
    [diagnostics, setDiagnostics] = useState(''),
    [publishing, setPublishing] = useState(false),
    [release, setRelease] = useState('1.0'),
    [description, setDescription] = useState(''),
    [visibility, setVisibility] = useState<'private' | 'unlisted' | 'public'>('private'),
    [published, setPublished] = useState<AiInfo>();
  const [small, setSmall] = useState(() => window.matchMedia('(max-width: 760px)').matches);
  const form = useRef<HTMLTextAreaElement>(null);
  const url = id ? ROOT + '/projects/' + id : '';
  const recoveryKey = account && id ? `ai-studio-draft:${account.id}:${id}` : '';
  const {
    project,
    source,
    saved,
    conflict,
    known,
    draft,
    saving,
    cursor,
    change,
    refresh: refreshDraft,
    save,
    reload,
  } = useProjectDraft(url, recoveryKey, setError);
  const recoveryLoaded = useRef(false);
  const submission = useRef<AiStudioCommand | undefined>(undefined);
  const purchases = useRef(new Map<string, { id: string; pack: string }>());
  const checkoutReturn = useRef('');
  const serverTitle = useRef<string | undefined>(undefined);
  const comparisonRequest = useRef(0);
  const currentRevision = () => {
    if (!known.current) throw Error('Project has not loaded.');
    return known.current.revision;
  };
  const active = project?.requests.find((r) => r.status === 'queued' || r.status === 'running');
  const locked = busy || !!active || conflict;
  const loadWallet = useCallback(async () => {
    const w = await request<AiStudioAccount>('GET', ROOT + '/account');
    setWallet(w);
    setBudget((b) => b || w.maxRequestCredits);
    return w;
  }, []);
  const refresh = useCallback(async () => {
    if (!id) return;
    const value = await refreshDraft();
    if (!value) return;
    if (!recoveryLoaded.current && recoveryKey) {
      recoveryLoaded.current = true;
      try {
        const pending = JSON.parse(
          sessionStorage.getItem(recoveryKey + ':request') ?? 'null',
        ) as AiStudioCommand | null;
        if (pending && !value.requests.some((r) => r.id === pending.id)) {
          submission.current = pending;
          setPrompt(pending.text);
          setBudget(pending.budget);
          setDiagnostics(pending.diagnostics ?? '');
          setError(
            'A previous submission was not acknowledged. Sending it again safely retries the same request.',
          );
        } else sessionStorage.removeItem(recoveryKey + ':request');
      } catch {
        /* Private browsing may disable local recovery. */
      }
    }
    if (submission.current && value.requests.some((r) => r.id === submission.current?.id)) {
      const sent = submission.current;
      submission.current = undefined;
      setPrompt((text) => (text === sent.text ? '' : text));
      setDiagnostics((text) => (text === (sent.diagnostics ?? '') ? '' : text));
      try {
        sessionStorage.removeItem(recoveryKey + ':request');
      } catch {
        /* Optional recovery. */
      }
    }
    const previousTitle = serverTitle.current;
    serverTitle.current = value.title;
    setTitle((local) =>
      previousTitle === undefined || local === previousTitle ? value.title : local,
    );
    setChecks((await request<{ items: Check[] }>('GET', url + '/checks')).items);
  }, [id, url, recoveryKey, refreshDraft]);
  useEffect(() => {
    if (
      !account ||
      !wallet ||
      !['returned', 'cancelled'].includes(location.search.get('payment') ?? '')
    )
      return;
    const returned = `${account.id}:${location.search.get('payment')}`;
    if (checkoutReturn.current === returned) return;
    checkoutReturn.current = returned;
    purchases.current.clear();
    for (const pack of wallet.packs) {
      try {
        sessionStorage.removeItem(checkoutKey(account.id, pack.id));
      } catch {
        /* Optional persistence. */
      }
    }
    const search = new URLSearchParams(location.search);
    search.delete('payment');
    navigate(location.path + (search.size ? '?' + search.toString() : ''), { replace: true });
  }, [account, wallet, location.search, location.path, navigate]);
  useEffect(() => {
    let mounted = true;
    void (async () => {
      if (!account) return;
      try {
        const w = await loadWallet();
        if (!mounted || !w.enabled) return;
        if (id) await refresh();
        else
          setProjects(
            (await request<{ items: AiStudioProject[] }>('GET', ROOT + '/projects')).items,
          );
      } catch (e) {
        if (mounted) setError(String(e));
      }
    })();
    return () => {
      mounted = false;
    };
  }, [account, id, loadWallet, refresh]);
  useEffect(() => {
    const m = window.matchMedia('(max-width: 760px)');
    const update = () => setSmall(m.matches);
    m.addEventListener('change', update);
    return () => m.removeEventListener('change', update);
  }, []);
  useEffect(() => {
    if (!id) return;
    let stopped = false,
      pending = false;
    const timer = setInterval(() => {
      if (pending || stopped || saving.current) return;
      pending = true;
      void request<{ events: { cursor: string }[] }>('GET', url + '/events', {
        query: { after: cursor.current },
      })
        .then(async (v) => {
          if (stopped) return;
          if (
            v.events.length ||
            known.current?.requests.some((r) => ['queued', 'running'].includes(r.status))
          ) {
            await refresh();
            await loadWallet();
          } else if (checks.some((c) => c.status === 'pending'))
            setChecks((await request<{ items: Check[] }>('GET', url + '/checks')).items);
        })
        .catch((e) => {
          if (!stopped) setError('Reconnecting: ' + String(e));
        })
        .finally(() => {
          pending = false;
        });
    }, 1500);
    return () => {
      stopped = true;
      clearInterval(timer);
    };
  }, [id, url, refresh, loadWallet, checks, cursor, known, saving]);
  useEffect(() => {
    if (!project || locked || source === project.current.source) return;
    const timer = setTimeout(() => void save().catch(() => {}), 900);
    return () => clearTimeout(timer);
  }, [source, project, locked, save]);
  const action = async (fn: () => Promise<void>) => {
    setBusy(true);
    setError('');
    try {
      await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };
  const create = async (file?: File) =>
    action(async () => {
      if (file && file.size > 131072)
        throw Error('The single JavaScript file must be at most 128 KiB.');
      const versionId = location.search.get('version');
      const p = await request<AiStudioProject>('POST', ROOT + '/projects', {
        body: {
          title,
          ...(file
            ? { source: new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer()) }
            : versionId
              ? { versionId }
              : {}),
        },
      });
      navigate('/ai-studio/' + p.id);
    });
  const revision = async (n: number) => {
    const generation = ++comparisonRequest.current;
    const source = n
      ? (await request<AiStudioRevision>('GET', url + '/revision', { query: { revision: n } }))
          .source
      : '';
    if (generation !== comparisonRequest.current) return;
    setSelected(n);
    setBaseline(source);
  };
  const restore = async (n: number) =>
    action(async () => {
      await save();
      await request('PATCH', url, {
        body: { expectedRevision: currentRevision(), restoreRevision: n },
      });
      await refresh();
    });
  const send = async () =>
    action(async () => {
      let command = submission.current;
      if (
        !command ||
        command.text !== prompt ||
        command.budget !== budget ||
        (command.diagnostics ?? '') !== diagnostics
      ) {
        await save();
        command = {
          id: crypto.randomUUID(),
          expectedRevision: currentRevision(),
          text: prompt,
          budget,
          ...(diagnostics ? { diagnostics } : {}),
        };
        submission.current = command;
        try {
          sessionStorage.setItem(recoveryKey + ':request', JSON.stringify(command));
        } catch {
          /* In-memory retries remain idempotent. */
        }
      }
      await request('POST', url + '/requests', { body: command });
      submission.current = undefined;
      try {
        sessionStorage.removeItem(recoveryKey + ':request');
      } catch {
        /* Optional recovery. */
      }
      const sent = command;
      setPrompt((text) => (text === sent.text ? '' : text));
      setDiagnostics((text) => (text === (sent.diagnostics ?? '') ? '' : text));
      await refresh();
    });
  const play = async () =>
    action(async () => {
      await save();
      const r = await request<Run>('POST', url + '/runs', {
        body: {
          id: crypto.randomUUID(),
          expectedRevision: currentRevision(),
          seed,
          opponent,
        },
      });
      setRun(r);
      setRunResult('');
      setTab('playtest');
    });
  const currentCheck = checks.find((c) => c.report.sourceHash === project?.current.hash);
  const fix = (text: string) => {
    setDiagnostics(text.slice(0, 16000));
    setPrompt('Please fix the issues in the attached test diagnostics.');
    form.current?.focus();
  };
  const publish = async () =>
    action(async () => {
      if (
        !currentCheck?.upload_id ||
        currentCheck.status !== 'valid' ||
        source !== project?.current.source
      )
        throw Error('Run compatibility checks on the current saved revision first.');
      setPublished(
        await aiApi.publish({
          uploadId: currentCheck.upload_id,
          name: title,
          description,
          tags: [],
          visibility,
          version: release,
          notes: `Created in AI Studio, revision ${project.revision}`,
        }),
      );
      setPublishing(false);
    });
  if (!account)
    return (
      <div className="notice">
        <h1>AI Studio</h1>
        <p>Create a colony AI through conversation.</p>
        <a href="/signin" className="btn primary">
          Sign in
        </a>
      </div>
    );
  if (wallet && !wallet.enabled)
    return (
      <div className="notice">
        <h1>AI Studio</h1>
        <p>This service is not enabled on this instance.</p>
      </div>
    );
  return (
    <div className="as-page">
      <header className="page-head">
        <div className="grow">
          <p className="caption">AI STUDIO · PRIVATE PROJECT</p>
          <h1>{id ? (project?.title ?? 'Loading project…') : 'Build a mind for your colony'}</h1>
          <p className="sub">Describe a strategy. Shape the code. Watch it play.</p>
        </div>
        <Link to="/ai-studio" className="btn">
          Projects
        </Link>
      </header>
      {error && (
        <div role="alert" className="notice">
          {error}
          {conflict && (
            <>
              <button onClick={() => download(source)}>Download local edits</button>
              <button
                onClick={() =>
                  void action(async () => {
                    await reload();
                    await refresh();
                  })
                }
              >
                Reload saved revision
              </button>
            </>
          )}
        </div>
      )}
      {!id ? (
        <>
          <section className="card">
            <h2>Start a project</h2>
            <label>
              Project name{' '}
              <input value={title} maxLength={128} onChange={(e) => setTitle(e.target.value)} />
            </label>
            <button
              className="primary"
              disabled={busy || !title.trim() || !wallet?.enabled}
              onClick={() => void create()}
            >
              {location.search.has('version') ? 'Copy my library version' : 'Use working starter'}
            </button>
            <label className="btn">
              Import .js
              <input
                aria-label="Import JavaScript"
                type="file"
                accept=".js"
                disabled={busy || !wallet?.enabled}
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  if (file) void create(file);
                  e.target.value = '';
                }}
              />
            </label>
            <p>One JavaScript file. Manual editing and local playtests use no credits.</p>
          </section>
          <div className="as-projects">
            {projects.map((p) => (
              <Link className="card" key={p.id} to={'/ai-studio/' + p.id}>
                <h2>{p.title}</h2>
                <p>Revision {p.revision}</p>
              </Link>
            ))}
          </div>
        </>
      ) : (
        project && (
          <>
            <div className="as-toolbar">
              <label>
                Project{' '}
                <input
                  value={title}
                  maxLength={128}
                  disabled={locked}
                  onChange={(e) => setTitle(e.target.value)}
                  onBlur={() => {
                    if (title.trim() && title !== project.title)
                      void action(async () => {
                        await save();
                        await request('PATCH', url, {
                          body: { expectedRevision: currentRevision(), title },
                        });
                        await refresh();
                      });
                  }}
                />
              </label>
              <span role="status">
                {saved} · revision {project.revision}
              </span>
              <button
                disabled={locked}
                onClick={() =>
                  void action(async () => {
                    await save();
                    await request('POST', url + '/check', {
                      body: { revision: currentRevision() },
                    });
                    await refresh();
                  })
                }
              >
                Run checks
              </button>
              <button disabled={locked} onClick={() => void play()}>
                Playtest
              </button>
              <button onClick={() => download(source)}>Download</button>
              <button
                disabled={
                  locked ||
                  source !== project.current.source ||
                  currentCheck?.status !== 'valid' ||
                  !currentCheck.upload_id
                }
                onClick={() => setPublishing(!publishing)}
              >
                Publish
              </button>
            </div>
            {published && (
              <p className="notice">
                Published <Link to={'/ais/' + published.id}>{published.name}</Link>.
              </p>
            )}
            {publishing && (
              <form
                className="card"
                onSubmit={(e) => {
                  e.preventDefault();
                  void publish();
                }}
              >
                <h2>Publish revision {project.revision}</h2>
                <label>
                  Version{' '}
                  <input
                    required
                    value={release}
                    maxLength={64}
                    onChange={(e) => setRelease(e.target.value)}
                  />
                </label>
                <label>
                  Description{' '}
                  <textarea
                    value={description}
                    maxLength={4000}
                    onChange={(e) => setDescription(e.target.value)}
                  />
                </label>
                <label>
                  Visibility{' '}
                  <select
                    value={visibility}
                    onChange={(e) => setVisibility(e.target.value as typeof visibility)}
                  >
                    <option value="private">Private</option>
                    <option value="unlisted">Unlisted</option>
                    <option value="public">Public</option>
                  </select>
                </label>
                <button disabled={busy}>Create library release</button>
              </form>
            )}
            <div
              className="as-workspace"
              style={{ gridTemplateColumns: small ? '1fr' : `${split}% 12px minmax(0,1fr)` }}
            >
              <section className="as-chat" aria-label="AI coding conversation">
                <div className="as-chat-history">
                  {project.requests.length === 0 && (
                    <div className="as-welcome">
                      <h2>What kind of colony will you build?</h2>
                      <p>Try “focus on food before expansion” or ask how the current code works.</p>
                      <p>The assistant edits your file. You decide when to test.</p>
                    </div>
                  )}
                  {project.requests.map((r) => (
                    <article key={r.id}>
                      <p className="as-user">{r.prompt}</p>
                      {r.diagnostics && (
                        <details>
                          <summary>Attached diagnostics</summary>
                          <pre>{r.diagnostics}</pre>
                        </details>
                      )}
                      <p className="as-response">
                        {r.response ||
                          ({ queued: 'Waiting to start…', running: 'Working on your AI…' }[
                            r.status
                          ] ??
                            r.status)}
                      </p>
                      {r.error && <p role="status">{r.error}</p>}
                      <small>
                        {r.status} · {r.charged === null ? 'usage pending' : r.charged + ' credits'}
                      </small>
                    </article>
                  ))}
                </div>
                <form
                  onSubmit={(e) => {
                    e.preventDefault();
                    void send();
                  }}
                >
                  <label htmlFor="as-prompt">Describe a change or ask a question</label>
                  <textarea
                    id="as-prompt"
                    ref={form}
                    value={prompt}
                    maxLength={16000}
                    onChange={(e) => setPrompt(e.target.value)}
                    placeholder="Make my colony more defensive…"
                  />
                  {diagnostics && (
                    <p>
                      Test diagnostics attached.{' '}
                      <button type="button" onClick={() => setDiagnostics('')}>
                        Remove
                      </button>
                    </p>
                  )}
                  <div className="as-toolbar">
                    <label>
                      Request cap{' '}
                      <input
                        type="number"
                        min={1}
                        max={wallet?.maxRequestCredits}
                        value={budget}
                        onChange={(e) => setBudget(Number(e.target.value))}
                      />
                    </label>
                    <button
                      className="primary"
                      disabled={
                        locked ||
                        !prompt.trim() ||
                        !wallet?.available ||
                        !Number.isInteger(budget) ||
                        budget < 1
                      }
                    >
                      Send
                    </button>
                    {active && (
                      <button
                        type="button"
                        onClick={() =>
                          void action(async () => {
                            await request('POST', url + '/stop');
                            await refresh();
                          })
                        }
                      >
                        Stop
                      </button>
                    )}
                  </div>
                </form>
                <details>
                  <summary>
                    {wallet?.available ?? 0} credits available · {wallet?.reserved ?? 0} reserved
                  </summary>
                  <p>
                    {wallet?.model} · per million tokens: {wallet?.rate?.input} input /{' '}
                    {wallet?.rate?.cachedInput} cached / {wallet?.rate?.output} output credits.
                  </p>
                  <p>Measured model usage is charged, including code that later fails tests.</p>
                  {wallet?.packs.map((p) => (
                    <button
                      key={p.id}
                      onClick={() =>
                        void action(async () => {
                          const key = checkoutKey(account.id, p.id);
                          const attempt =
                            purchases.current.get(key) ?? checkoutAttempt(account.id, p.id);
                          purchases.current.set(key, attempt);
                          try {
                            const v = await request<{ url: string }>('POST', ROOT + '/checkout', {
                              body: attempt,
                            });
                            window.location.assign(v.url);
                          } catch (e) {
                            if (e instanceof ApiError && e.status === 409) {
                              purchases.current.delete(key);
                              try {
                                sessionStorage.removeItem(key);
                              } catch {
                                /* Optional persistence. */
                              }
                            }
                            throw e;
                          }
                        })
                      }
                    >
                      {p.credits} credits · {(p.amount / 100).toFixed(2)} {p.currency.toUpperCase()}
                    </button>
                  ))}
                </details>
              </section>
              {!small && (
                <div
                  className="as-divider"
                  role="separator"
                  aria-label="Resize conversation"
                  aria-orientation="vertical"
                  aria-valuemin={25}
                  aria-valuemax={60}
                  aria-valuenow={split}
                  tabIndex={0}
                  onKeyDown={(e) => {
                    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
                      e.preventDefault();
                      setSplit((v) =>
                        Math.max(25, Math.min(60, v + (e.key === 'ArrowLeft' ? -2 : 2))),
                      );
                    }
                  }}
                  onPointerDown={(e) => e.currentTarget.setPointerCapture(e.pointerId)}
                  onPointerMove={(e) => {
                    if (e.currentTarget.hasPointerCapture(e.pointerId)) {
                      const rect = e.currentTarget.parentElement?.getBoundingClientRect();
                      if (!rect) return;
                      setSplit(
                        Math.max(25, Math.min(60, ((e.clientX - rect.left) / rect.width) * 100)),
                      );
                    }
                  }}
                  onPointerUp={(e) => e.currentTarget.releasePointerCapture(e.pointerId)}
                />
              )}
              <section className="as-code-pane">
                <nav className="seg" aria-label="Workspace view">
                  {(['code', 'changes', 'playtest'] as const).map((t) => (
                    <button
                      key={t}
                      aria-pressed={tab === t}
                      onClick={() => {
                        setTab(t);
                        if (t === 'changes' && !selected)
                          void action(() =>
                            revision(project.revisions[1]?.revision ?? project.revision),
                          );
                      }}
                    >
                      {t === 'code' ? 'Code' : t === 'changes' ? 'Changes' : 'Playtest'}
                    </button>
                  ))}
                </nav>
                <div className="as-toolbar">
                  <strong>ai.js</strong>
                  <label>
                    History{' '}
                    <select
                      value={selected}
                      onChange={(e) => void action(() => revision(Number(e.target.value)))}
                    >
                      <option value={0}>Choose revision</option>
                      {project.revisions.map((r) => (
                        <option key={r.revision} value={r.revision}>
                          r{r.revision} · {r.reason}
                        </option>
                      ))}
                    </select>
                  </label>
                  <button disabled={locked || !selected} onClick={() => void restore(selected)}>
                    Restore
                  </button>
                  <button
                    disabled={locked || project.revisions.length < 2}
                    onClick={() => void restore(project.revisions[1]?.revision ?? project.revision)}
                  >
                    Undo revision
                  </button>
                  <label className="btn">
                    Import
                    <input
                      aria-label="Replace source with JavaScript file"
                      type="file"
                      accept=".js"
                      disabled={locked}
                      onChange={(e) => {
                        const file = e.target.files?.[0];
                        if (file)
                          void action(async () => {
                            if (file.size > 131072) throw Error('Maximum source size is 128 KiB.');
                            await save();
                            const source = new TextDecoder('utf-8', { fatal: true }).decode(
                              await file.arrayBuffer(),
                            );
                            await request('PATCH', url, {
                              body: {
                                expectedRevision: currentRevision(),
                                source,
                                reason: 'import',
                              },
                            });
                            await refresh();
                          });
                        e.target.value = '';
                      }}
                    />
                  </label>
                </div>
                <div hidden={tab !== 'code'}>
                  {small ? (
                    <textarea
                      className="as-source"
                      aria-label="AI JavaScript source"
                      spellCheck={false}
                      readOnly={locked}
                      value={source}
                      onChange={(e) => change(e.target.value)}
                    />
                  ) : (
                    <Suspense fallback={<p>Loading code editor…</p>}>
                      <Editor
                        source={source}
                        readOnly={locked}
                        onChange={(text) => {
                          if (text !== draft.current) change(text);
                        }}
                      />
                    </Suspense>
                  )}
                </div>
                {tab === 'changes' &&
                  (small ? (
                    <>
                      <textarea
                        className="as-source"
                        aria-label="Current AI JavaScript source"
                        spellCheck={false}
                        readOnly
                        value={source}
                      />
                      <details>
                        <summary>Compared revision {selected}</summary>
                        <pre>{baseline}</pre>
                      </details>
                    </>
                  ) : (
                    <Suspense fallback={<p>Loading revision comparison…</p>}>
                      <Editor source={source} baseline={baseline} readOnly onChange={() => {}} />
                    </Suspense>
                  ))}
                <div hidden={tab !== 'playtest'}>
                  <div className="as-toolbar">
                    <label>
                      Seed{' '}
                      <input
                        type="number"
                        min={0}
                        max={4294967295}
                        value={seed}
                        onChange={(e) => setSeed(Number(e.target.value))}
                      />
                    </label>
                    <label>
                      Opponent{' '}
                      <select value={opponent} onChange={(e) => setOpponent(e.target.value)}>
                        <option value="numbi">Numbi</option>
                        <option value="nicowar">Nicowar</option>
                      </select>
                    </label>
                    <button disabled={locked} onClick={() => void play()}>
                      {run ? 'Run current revision' : 'Start live game'}
                    </button>
                    {run && (
                      <>
                        <button
                          disabled={busy}
                          onClick={() =>
                            void action(async () => {
                              const next = await request<Run>('POST', url + '/runs', {
                                body: {
                                  id: crypto.randomUUID(),
                                  expectedRevision: run.revision,
                                  seed: run.seed,
                                  opponent: run.opponent,
                                },
                              });
                              setRun(next);
                            })
                          }
                        >
                          Restart same setup
                        </button>
                        <button onClick={() => setRun(undefined)}>Stop game</button>
                      </>
                    )}
                  </div>
                  {run ? (
                    <Playtest
                      key={run.runId}
                      run={run}
                      onResult={(text) => {
                        setRunResult(text);
                        void request('POST', url + '/run-result', {
                          body: { runId: run.runId, summary: text },
                        }).catch((e) => setError(String(e)));
                      }}
                    />
                  ) : (
                    <p className="as-welcome">
                      Watch your AI against a built-in opponent on the fixed test map. The game runs
                      on this computer and ends when you close it.
                    </p>
                  )}
                  {runResult && <button onClick={() => fix(runResult)}>Fix this</button>}
                </div>
                {currentCheck && (
                  <details className="as-checks" open={currentCheck.status === 'invalid'}>
                    <summary>
                      Revision {project.revision} checks: {currentCheck.status}
                    </summary>
                    <AiChecklist report={currentCheck.report} />
                    {currentCheck.error && <p>{currentCheck.error}</p>}
                    <button onClick={() => fix(JSON.stringify(currentCheck.report))}>
                      Fix this
                    </button>
                  </details>
                )}
              </section>
            </div>
            <details>
              <summary>Project actions</summary>
              <button
                disabled={locked}
                onClick={() =>
                  void action(async () => {
                    if (!window.confirm('Delete this private project and its history?')) return;
                    await request('DELETE', url);
                    navigate('/ai-studio');
                  })
                }
              >
                Delete project
              </button>
            </details>
          </>
        )
      )}
    </div>
  );
}
