import { useCallback, useEffect, useRef, useState } from 'react';
import type { BuildingAiStudioThread, BuildingAiStudioTurn } from '@glob2/protocol';
import { ApiError, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
import { BuildingPreview } from './building-ai/Preview.tsx';
import { readSavedTurn, rememberTurn } from './building-ai/retryStore.ts';
import '../styles/ai-building-studio.css';
import {
  BUILDING_STUDIO_ROOT as ROOT,
  useStudioSnapshot,
} from './building-ai/useStudioSnapshot.ts';
export function AiBuildingStudio({ id }: { id?: string }) {
  const { account } = useSession();
  // Account changes must discard private snapshots and unsent request state,
  // even when the router stays on the same project URL.
  return <BuildingWorkspace key={`${account?.id ?? 'anonymous'}:${id ?? 'projects'}`} id={id} />;
}

function BuildingWorkspace({ id }: { id?: string }) {
  const { account } = useSession(),
    { navigate, location } = useRouter();
  const [text, setText] = useState(''),
    [title, setTitle] = useState('New building family'),
    [error, setError] = useState(''),
    [notice, setNotice] = useState(''),
    [busy, setBusy] = useState(false),
    [selectedRefs, setSelectedRefs] = useState<string[]>([]),
    [retrying, setRetrying] = useState(false),
    [confirmDelete, setConfirmDelete] = useState(false);
  const messages = useRef<HTMLDivElement>(null),
    followMessages = useRef(true);
  const [pane, setPane] = useState<'chat' | 'preview'>('chat'),
    [selected, setSelected] = useState(''),
    [compare, setCompare] = useState('');
  const submission = useRef<BuildingAiStudioTurn | null>(null),
    creation = useRef(crypto.randomUUID());
  const reconcileSubmission = useCallback(
    (snapshot: BuildingAiStudioThread) => {
      // The server may accept a turn even when the POST response is lost. Its
      // persisted UUID clears the local retry without dispatching another build.
      if (submission.current && snapshot.requests.some((r) => r.id === submission.current?.id)) {
        submission.current = null;
        setRetrying(false);
        rememberTurn(`ai-building-studio-turn:${account?.id}:${id}`, null);
        setText('');
      }
    },
    [account?.id, id],
  );
  const { wallet, projects, thread, draft, progress, loading, loadError, connection, refresh } =
    useStudioSnapshot(account?.id, id, reconcileSubmission);
  const pending = thread?.requests.findLast((r) => !['ready', 'failed'].includes(r.status));
  useEffect(() => {
    if (!account || !id) return;
    const key = `ai-building-studio-turn:${account.id}:${id}`;
    const saved = readSavedTurn(key);
    if (saved) {
      submission.current = saved;
      queueMicrotask(() => {
        setText(saved.text);
        setSelectedRefs(saved.references);
        setRetrying(true);
      });
    }
  }, [account, id]);
  useEffect(() => {
    if (followMessages.current && messages.current)
      messages.current.scrollTop = messages.current.scrollHeight;
  }, [thread?.messages]);
  async function action(fn: () => Promise<void>) {
    setBusy(true);
    setError('');
    setNotice('');
    try {
      await fn();
      await refresh();
    } catch (e) {
      // A revision conflict should refresh immediately so the preserved prompt
      // can be retried against the current draft rather than waiting for polling.
      if (e instanceof ApiError && e.status === 409) await refresh().catch(() => undefined);
      setError(e instanceof Error ? e.message : 'Request failed.');
    } finally {
      setBusy(false);
    }
  }
  async function create() {
    const result = await request<{ id: string }>('POST', ROOT + '/threads', {
      body: {
        id: creation.current,
        title,
        ...(location.search.get('draft') ? { draftId: location.search.get('draft') } : {}),
      },
    });
    navigate('/ai-building-studio/' + result.id);
  }
  async function send() {
    if (!draft || !account || !id) return;
    const value = submission.current ?? {
      id: crypto.randomUUID(),
      text,
      expectedRevision: draft.revision,
      references: selectedRefs,
    };
    submission.current = value;
    setRetrying(true);
    rememberTurn(`ai-building-studio-turn:${account.id}:${id}`, value);
    followMessages.current = true;
    try {
      await request('POST', `${ROOT}/threads/${id}/turns`, { body: value });
    } catch (e) {
      // A definite rejection did not accept this turn. Keep its prompt editable;
      // transport/server failures retain the UUID to safely retry an unknown outcome.
      if (e instanceof ApiError && e.status >= 400 && e.status < 500 && e.status !== 408) {
        submission.current = null;
        setRetrying(false);
        rememberTurn(`ai-building-studio-turn:${account.id}:${id}`, null);
      }
      throw e;
    }
    submission.current = null;
    setRetrying(false);
    rememberTurn(`ai-building-studio-turn:${account.id}:${id}`, null);
    setText('');
  }
  async function restoreRevision(requestId: string) {
    if (!draft || !id) return;
    // Optimistic revision checks protect edits made in the manual studio or
    // another tab. Restoring never spends a generation credit.
    await request('POST', `${ROOT}/threads/${id}/requests/${requestId}/adopt`, {
      body: { expectedRevision: draft.revision },
    });
    setSelected('');
    setNotice('Revision restored to your saved draft.');
  }
  const selectedRevision = thread?.revisions.find((r) => r.requestId === selected);
  const compared =
    compare !== selected ? thread?.revisions.find((r) => r.requestId === compare) : undefined;
  const currentLabel = selectedRevision?.title ?? 'Current saved draft';
  if (!account)
    return (
      <section>
        <h1>AI Building Studio</h1>
        <p>Describe a building. Create its artwork and the way it works.</p>
        <a href="/signin">Sign in to create a building</a>
      </section>
    );
  return (
    <section className="ai-building-studio">
      <header className="bas-header">
        <div>
          <Link to="/buildings">Building library</Link>
          <h1>{thread?.title ?? 'AI Building Studio'}</h1>
          <p>Create a building’s appearance and gameplay properties.</p>
        </div>
        <div>
          <strong>{wallet ? `${wallet.available} credits available` : 'Loading credits…'}</strong>
          <p>{wallet ? `${wallet.reserved} reserved · ` : ''}1 credit per delivered generation</p>
        </div>
      </header>
      {(error || loadError) && <p role="alert">{error || loadError}</p>}
      {notice && <p role="status">{notice}</p>}
      {loading && <p role="status">Loading your building workspace…</p>}
      {connection && <p role="status">{connection}</p>}
      {wallet && !wallet.enabled && (
        <p role="status">
          Building Studio is disabled on this instance. Your saved projects remain available.
        </p>
      )}
      <details className="bas-credits">
        <summary>Building credits</summary>
        <p>
          Questions and brainstorming are free. A creation or revision request starts one build of
          up to 12 building variants. Failed generations return their reservation.
        </p>
        {wallet?.packs.map((p) => (
          <button
            key={p.id}
            disabled={busy || !wallet.enabled}
            onClick={() =>
              void action(async () => {
                const r = await request<{ url: string }>('POST', ROOT + '/checkout', {
                  body: { pack: p.id },
                });
                window.location.assign(r.url);
              })
            }
          >
            Buy {p.credits} credits ·{' '}
            {new Intl.NumberFormat(undefined, { style: 'currency', currency: p.currency }).format(
              p.amount / 100,
            )}
          </button>
        ))}
      </details>
      {!id ? (
        <>
          <div className="bas-start">
            <h2>
              {location.search.has('draft')
                ? 'Edit your building with AI'
                : 'What building will you create?'}
            </h2>
            <p>
              Try a mushroom hospital, a fortified kitchen, or a training lodge. Describe what it
              should do and how it should look.
            </p>
            <label>
              Project title
              <input maxLength={128} value={title} onChange={(e) => setTitle(e.target.value)} />
            </label>
            <button
              className="primary"
              disabled={busy || !wallet?.enabled || !title.trim()}
              onClick={() => void action(create)}
            >
              Start creating
            </button>
          </div>
          <h2>Your projects</h2>
          {!loading && !projects.length && <p>Your first project will appear here.</p>}
          <div className="bas-projects">
            {projects.map((p) => (
              <Link className="card" key={p.id} to={'/ai-building-studio/' + p.id}>
                {p.title}
              </Link>
            ))}
          </div>
        </>
      ) : (
        <div className="building-ai-workspace">
          <nav className="building-ai-tabs" aria-label="Workspace panes">
            <button aria-pressed={pane === 'chat'} onClick={() => setPane('chat')}>
              Conversation
            </button>
            <button aria-pressed={pane === 'preview'} onClick={() => setPane('preview')}>
              Building preview
            </button>
          </nav>
          <div className="bas-layout" data-pane={pane}>
            <aside className="bas-chat">
              <h2>Design conversation</h2>
              <details className="building-ai-project-options">
                <summary>Project options</summary>
                <p>Your saved building remains available in Building Studio.</p>
                {confirmDelete ? (
                  <div role="group" aria-label="Confirm history deletion">
                    <p>
                      Delete this project’s conversation, references and generated revisions
                      permanently?
                    </p>
                    <button
                      disabled={busy || !!pending}
                      onClick={() =>
                        void action(async () => {
                          await request('DELETE', `${ROOT}/threads/${id}`);
                          navigate('/ai-building-studio');
                        })
                      }
                    >
                      Delete history permanently
                    </button>
                    <button disabled={busy} onClick={() => setConfirmDelete(false)}>
                      Keep history
                    </button>
                  </div>
                ) : (
                  <button disabled={busy || !!pending} onClick={() => setConfirmDelete(true)}>
                    Delete project history
                  </button>
                )}
              </details>
              <div
                className="bas-messages"
                aria-live="polite"
                ref={messages}
                onScroll={() => {
                  const box = messages.current;
                  if (box)
                    followMessages.current =
                      box.scrollHeight - box.scrollTop - box.clientHeight < 48;
                }}
              >
                {thread?.messages.map((m) => (
                  <article key={m.id} className={'bas-message ' + m.role}>
                    <strong>{m.role === 'user' ? 'You' : 'Building designer'}</strong>
                    <p>{m.text}</p>
                  </article>
                ))}
                {thread && !thread.messages.length && (
                  <p className="building-ai-empty">
                    Describe what your building should do and how it should look. You can also
                    brainstorm before creating it.
                  </p>
                )}
              </div>
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  void action(send);
                }}
              >
                <label>
                  Describe your creation or ask a question
                  <textarea
                    rows={5}
                    maxLength={8000}
                    value={text}
                    disabled={retrying}
                    onChange={(e) => setText(e.target.value)}
                    placeholder="Create a mushroom hospital that heals workers and warriors, costs 10 wood, and holds four units."
                  />
                </label>
                <p className="building-ai-cost">
                  Questions are free. Creation and edit requests start automatically and cost 1
                  credit on delivery.
                </p>
                <label>
                  Reference images
                  <input
                    type="file"
                    accept="image/png,image/jpeg,image/webp"
                    disabled={busy || retrying || !wallet?.enabled}
                    onChange={(e) => {
                      const file = e.target.files?.[0];
                      if (file)
                        void action(async () => {
                          const ref = await request<{ hash: string }>(
                            'POST',
                            `${ROOT}/threads/${id}/references`,
                            { body: file },
                          );
                          setSelectedRefs((v) => [...new Set([...v, ref.hash])].slice(-4));
                        });
                      e.target.value = '';
                    }}
                  />
                </label>
                <small>
                  Upload artwork you may use as a reference. Up to four selected images guide the
                  visual style.
                </small>
                <div className="bas-references">
                  {thread?.references.map((r) => (
                    <label key={r.hash}>
                      <img src={r.url} alt={r.label} />
                      <input
                        type="checkbox"
                        checked={selectedRefs.includes(r.hash)}
                        disabled={
                          retrying || (!selectedRefs.includes(r.hash) && selectedRefs.length >= 4)
                        }
                        onChange={(e) =>
                          setSelectedRefs((v) =>
                            e.target.checked ? [...v, r.hash] : v.filter((h) => h !== r.hash),
                          )
                        }
                      />
                      Use reference
                    </label>
                  ))}
                </div>
                <button
                  className="primary"
                  disabled={busy || !!pending || !wallet?.enabled || !text.trim() || !draft}
                >
                  {retrying ? 'Retry saved request' : 'Send'}
                </button>
                {retrying && (
                  <button
                    type="button"
                    disabled={busy}
                    onClick={() => {
                      submission.current = null;
                      setRetrying(false);
                      rememberTurn(`ai-building-studio-turn:${account.id}:${id}`, null);
                    }}
                  >
                    Discard local retry
                  </button>
                )}
              </form>
              {pending && (
                <div role="status">
                  <p>
                    {pending.status === 'uncertain'
                      ? 'This request is paused while its result is checked. Your reserved credit stays safe; you can leave and return later.'
                      : 'Working on your request…'}
                  </p>
                  <button
                    disabled={busy || ['dispatched', 'uncertain'].includes(pending.status)}
                    onClick={() =>
                      void action(async () => {
                        await request(
                          'POST',
                          `${ROOT}/threads/${id}/requests/${pending.id}/cancel`,
                          {
                            body: {},
                          },
                        );
                      })
                    }
                  >
                    Cancel request
                  </button>
                </div>
              )}
            </aside>
            <div className="bas-preview">
              <h2>Your building</h2>
              {progress && (
                <>
                  <ol className="bas-stages">
                    {progress.stages.map((s) => (
                      <li key={s.id} data-status={s.status}>
                        <strong>{s.label}</strong>
                        <span>{s.status}</span>
                        {s.detail && <small>{s.detail}</small>}
                      </li>
                    ))}
                  </ol>
                  {progress.notes.slice(-3).map((n, i) => (
                    <p role="status" key={i}>
                      {n.text}
                    </p>
                  ))}
                  {progress.checks.map((c) => (
                    <p key={c.id}>
                      {c.label}: {c.status} · {c.detail}
                    </p>
                  ))}
                </>
              )}
              {thread?.requests.at(-1)?.error && (
                <p role="alert">{thread.requests.at(-1)?.error}</p>
              )}
              {draft && (
                <>
                  <label>
                    View revision
                    <select value={selected} onChange={(e) => setSelected(e.target.value)}>
                      <option value="">Current saved draft</option>
                      {thread?.revisions.map((r, i) => (
                        <option key={r.requestId} value={r.requestId}>
                          {r.title} · revision {thread.revisions.length - i}
                        </option>
                      ))}
                    </select>
                  </label>
                  <BuildingPreview
                    pack={selectedRevision?.package ?? draft.package}
                    assetRoot={
                      selectedRevision
                        ? `${ROOT}/threads/${id}/revisions/${selected}/assets`
                        : `/api/v1/building-drafts/${draft.id}/assets`
                    }
                  />
                  {selectedRevision && (
                    <p role="status">
                      Viewing a saved revision. Restore it to make it the current draft.
                    </p>
                  )}
                  <label>
                    Compare with
                    <select
                      value={compare === selected ? '' : compare}
                      onChange={(e) => setCompare(e.target.value)}
                    >
                      <option value="">No comparison</option>
                      {thread?.revisions
                        .filter((r) => r.requestId !== selected)
                        .map((r) => (
                          <option key={r.requestId} value={r.requestId}>
                            {r.title}
                          </option>
                        ))}
                    </select>
                  </label>
                  {compared && (
                    <section aria-label={'Comparison with ' + compared.title}>
                      <p className="building-ai-action-hint">Compared revision: {compared.title}</p>
                      <BuildingPreview
                        pack={compared.package}
                        assetRoot={`${ROOT}/threads/${id}/revisions/${compare}/assets`}
                        comparisonLabel={currentLabel}
                        comparison={selectedRevision?.package ?? draft.package}
                      />
                    </section>
                  )}
                  <div className="building-ai-actions">
                    {selectedRevision && (
                      <>
                        <a
                          className="btn"
                          href={`${ROOT}/threads/${id}/revisions/${selectedRevision.requestId}/file`}
                        >
                          Export viewed revision
                        </a>
                        <button
                          disabled={busy || !!pending}
                          onClick={() =>
                            void action(() => restoreRevision(selectedRevision.requestId))
                          }
                        >
                          Use this revision
                        </button>
                      </>
                    )}
                  </div>
                  <p className="building-ai-action-hint">
                    Edit, export or publish the current saved draft:
                  </p>
                  <div className="building-ai-actions">
                    <Link className="btn" to={'/building-studio/' + draft.id}>
                      Open in Building Studio
                    </Link>
                    <a className="btn" href={'/api/v1/building-drafts/' + draft.id + '/archive'}>
                      Export package
                    </a>
                    <Link className="btn" to={'/building-studio/' + draft.id}>
                      Review and publish
                    </Link>
                  </div>
                </>
              )}
              {!!thread?.revisions.length && (
                <details className="building-ai-history">
                  <summary>Generated revisions</summary>
                  {thread.revisions.map((r) => (
                    <article key={r.requestId}>
                      <h3>{r.title}</h3>
                      <p>
                        {r.applied ? 'Delivered to draft' : 'Saved candidate'} ·{' '}
                        {r.report.valid ? 'Engine validation passed' : 'Validation failed'}
                      </p>
                      <a href={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`}>
                        Download candidate
                      </a>
                      <button
                        disabled={busy || !!pending || !draft}
                        onClick={() => void action(() => restoreRevision(r.requestId))}
                      >
                        Restore this revision
                      </button>
                    </article>
                  ))}
                </details>
              )}
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
