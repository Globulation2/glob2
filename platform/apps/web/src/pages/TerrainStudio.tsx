import { cancellationReason } from '../components/studio/adapters.ts';
import { useRevisionUndo } from '../components/studio/useRevisionUndo.ts';
import {
  StudioShell,
  StudioHeader,
  StudioWorkspace,
  StudioTabs,
  ChatInput,
  ConversationPane,
  ReleaseDialog,
  CreditPanel,
  NewStudio,
  RevisionControls,
  ValidationSummary,
} from '../components/studio/Studio.tsx';
import { studioSession, useStudioValue } from '../components/studio/storage.ts';
import { Icon } from '../icons.tsx';
import { useCallback, useEffect, useRef, useState } from 'react';
import type {
  SetDraft,
  SetPackage,
  TerrainStudioThread,
  TerrainStudioProgress,
  TerrainStudioTurn,
} from '@glob2/protocol';
import { ApiError, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
import { SetEditor } from '../sets/Workspace.tsx';
import { SetPreview } from '../sets/Preview.tsx';
import '../styles/terrain-studio.css';
const ROOT = '/api/v1/terrain-studio';
type Wallet = {
  enabled: boolean;
  available: number;
  reserved: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
};
export function TerrainStudio({ id }: { id?: string }) {
  const { account } = useSession();
  return <TerrainWorkspace key={`${account?.id ?? 'anonymous'}:${id ?? 'new'}`} id={id} />;
}
function TerrainWorkspace({ id }: { id?: string }) {
  const { account } = useSession(),
    { navigate, location } = useRouter();
  const [wallet, setWallet] = useState<Wallet>(),
    [projects, setProjects] = useState<{ id: string; title: string }[]>([]),
    [thread, setThread] = useState<TerrainStudioThread>(),
    [draft, setDraft] = useState<SetDraft>(),
    [progress, setProgress] = useState<TerrainStudioProgress>(),
    [title, setTitle] = useState('New terrain set'),
    [error, setError] = useState(''),
    [connection, setConnection] = useState(''),
    [busy, setBusy] = useState(false),
    [dirty, setDirty] = useState(false);
  const [text, setText] = useStudioValue(`terrain-studio-prompt:${account?.id}:${id ?? 'new'}`, '');
  const [retrying, setRetrying] = useState(false);
  const [inspectorRevision, setInspectorRevision] = useState<number>();
  const [selectedRefs, setSelectedRefs] = useStudioValue<string[]>(
    `terrain-studio-references:${account?.id}:${id ?? 'new'}`,
    [],
  );
  const [view, setView] = useState('preview');
  const [creditsOpen, setCreditsOpen] = useState(false);
  const [focusChat, setFocusChat] = useState(0);
  const [inspected, setInspected] = useStudioValue(`terrain-view:${account?.id}:${id}`, '');
  const [historical, setHistorical] = useState<{
    id: string;
    pack: SetPackage;
    progress: TerrainStudioProgress;
  }>();
  useEffect(() => {
    if (!inspected || !id) return;
    const abort = new AbortController();
    void Promise.all([
      request<SetPackage>('GET', `${ROOT}/threads/${id}/revisions/${inspected}/file`, {
        signal: abort.signal,
      }),
      request<TerrainStudioProgress>(
        'GET',
        `${ROOT}/threads/${id}/requests/${inspected}/progress`,
        { signal: abort.signal },
      ),
    ])
      .then(([pack, progress]) => {
        if (!abort.signal.aborted) setHistorical({ id: inspected, pack, progress });
      })
      .catch((e) => {
        if (!abort.signal.aborted) setError(e.message);
      });
    return () => abort.abort();
  }, [id, inspected]);
  const dirtyRef = useRef(false);
  const manualDirty = useCallback((value: boolean) => {
    dirtyRef.current = value;
    setDirty(value);
  }, []);
  const [creationId] = useStudioValue(`terrain-studio-create:${account?.id}`, crypto.randomUUID());
  const submission = useRef<TerrainStudioTurn | null>(null),
    creation = useRef(creationId),
    serial = useRef(0);
  const pending = thread?.requests.findLast((r) => !['ready', 'failed'].includes(r.status));
  const latestGeneration = thread?.requests.filter((r) => r.kind === 'generate').at(-1);
  const refresh = useCallback(
    async (signal?: AbortSignal) => {
      const ticket = ++serial.current;
      const w = await request<Wallet>('GET', ROOT + '/account', { signal });
      if (signal?.aborted || ticket !== serial.current) return;
      setWallet(w);
      if (!id) {
        const list = await request<{ items: { id: string; title: string }[] }>(
          'GET',
          ROOT + '/threads',
          { signal },
        );
        if (!signal?.aborted) setProjects(list.items);
        return;
      }
      const t = await request<TerrainStudioThread>('GET', `${ROOT}/threads/${id}`, { signal });
      const d = await request<SetDraft>('GET', '/api/v1/set-drafts/' + t.draftId, { signal });
      const last = t.requests.at(-1);
      const p = last
        ? await request<TerrainStudioProgress>(
            'GET',
            `${ROOT}/threads/${id}/requests/${last.id}/progress`,
            { signal },
          )
        : undefined;
      if (signal?.aborted || ticket !== serial.current) return;
      setThread(t);
      setDraft(d);
      if (!dirtyRef.current) setInspectorRevision(d.revision);
      setProgress(p);
      const key = account && id ? `terrain-studio-turn:${account.id}:${id}` : '';
      if (submission.current && t.requests.some((r) => r.id === submission.current?.id)) {
        submission.current = null;
        setRetrying(false);
        studioSession.removeItem(key);
        setText('');
      }
    },
    [id, account, setText],
  );
  useEffect(() => {
    if (!account) return;
    const abort = new AbortController();
    let stream: EventSource | undefined;
    const update = () => {
      void refresh(abort.signal).catch((e) => {
        if (!abort.signal.aborted) setError(e.message);
      });
    };
    update();
    if (id && typeof EventSource !== 'undefined') {
      stream = new EventSource(`${ROOT}/threads/${id}/events`);
      stream.onmessage = update;
      stream.onopen = () => {
        setConnection('');
        update();
      };
      stream.onerror = () => setConnection('Reconnecting to your saved project…');
      stream.addEventListener('reset', update);
    }
    const poll = setInterval(update, 10000);
    return () => {
      abort.abort();
      stream?.close();
      clearInterval(poll);
    };
  }, [account, id, refresh]);
  useEffect(() => {
    if (!account || !id) return;
    const key = `terrain-studio-turn:${account.id}:${id}`;
    try {
      const value = studioSession.getItem(key);
      if (value) {
        const saved = JSON.parse(value) as TerrainStudioTurn;
        submission.current = saved;
        queueMicrotask(() => {
          setText(saved.text);
          setSelectedRefs(saved.references);
          setRetrying(true);
        });
      }
    } catch {
      studioSession.removeItem(key);
    }
  }, [account, id, setSelectedRefs, setText]);
  async function action(fn: () => Promise<void>) {
    setBusy(true);
    setError('');
    try {
      await fn();
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Request failed.');
    } finally {
      setBusy(false);
    }
  }
  const undo = useRevisionUndo(
    `terrain-undo:${account?.id}:${id}`,
    draft,
    thread?.revisions.filter(
      (r) =>
        r.applied &&
        draft?.revision === r.baseRevision + 1 &&
        r.report.hash === draft.validation?.hash,
    ) ?? [],
  );
  async function create() {
    const result = await request<{ id: string }>('POST', ROOT + '/threads', {
      body: {
        id: creation.current,
        title,
        ...(location.search.get('draft') ? { draftId: location.search.get('draft') } : {}),
        ...(location.search.get('version') ? { versionId: location.search.get('version') } : {}),
      },
    });
    studioSession.removeItem(`terrain-studio-create:${account?.id}`);
    if (text.trim() && account) {
      const thread = await request<{ draftId: string }>('GET', `${ROOT}/threads/${result.id}`);
      const initial = await request<{ revision: number }>(
        'GET',
        '/api/v1/set-drafts/' + thread.draftId,
      );
      const turn = {
        id: crypto.randomUUID(),
        text,
        references: selectedRefs,
        expectedRevision: initial.revision,
      };
      studioSession.setItem(`terrain-studio-turn:${account.id}:${result.id}`, JSON.stringify(turn));
      try {
        await request('POST', `${ROOT}/threads/${result.id}/turns`, { body: turn });
        studioSession.removeItem(`terrain-studio-turn:${account.id}:${result.id}`);
      } finally {
        setText('');
        navigate('/terrain-studio/' + result.id);
      }
    } else navigate('/terrain-studio/' + result.id);
  }
  async function send() {
    if (!draft || !account || !id || pending || dirty || !wallet?.enabled) return;
    if (!wallet.available) {
      setCreditsOpen(true);
      return;
    }
    if (!submission.current && !text.trim()) return;
    const value = submission.current ?? {
      id: crypto.randomUUID(),
      text,
      expectedRevision: draft.revision,
      references: selectedRefs,
    };
    if (!submission.current) undo.remember();
    submission.current = value;
    setRetrying(true);
    studioSession.setItem(`terrain-studio-turn:${account.id}:${id}`, JSON.stringify(value));
    try {
      await request('POST', `${ROOT}/threads/${id}/turns`, { body: value });
    } catch (e) {
      // A definite rejection did not accept this turn. Keep its prompt editable;
      // transport/server failures retain the UUID to safely retry an unknown outcome.
      if (e instanceof ApiError && e.status >= 400 && e.status < 500 && e.status !== 408) {
        submission.current = null;
        setRetrying(false);
        studioSession.removeItem(`terrain-studio-turn:${account.id}:${id}`);
      }
      throw e;
    }
    submission.current = null;
    setRetrying(false);
    studioSession.removeItem(`terrain-studio-turn:${account.id}:${id}`);
    setText('');
  }
  const viewedRevision = thread?.revisions.find((r) => r.requestId === inspected);
  const shownProgress = inspected
    ? historical?.id === inspected
      ? historical.progress
      : undefined
    : progress;
  const shownDraft = inspected
    ? historical?.id === inspected && draft
      ? { ...draft, package: historical.pack }
      : undefined
    : draft;
  if (!account)
    return (
      <section>
        <h1>AI Terrain Studio</h1>
        <p>Describe a world. Create its terrain and resources.</p>
        <a href="/signin">Sign in to create a set</a>
      </section>
    );
  return (
    <StudioShell className="terrain-studio">
      <StudioHeader title={thread?.title ?? 'AI Terrain Studio'} icon="mountain">
        <Link to="/terrain-studio">
          <Icon name="folder-open" size={18} /> Projects
        </Link>
        <button onClick={() => setCreditsOpen(true)}>
          <Icon name="coins" size={18} /> {wallet?.available ?? '…'} Terrain credits
        </button>
      </StudioHeader>
      {error && <p role="alert">{error}</p>}
      {connection && <p role="status">{connection}</p>}
      {wallet && !wallet.enabled && (
        <p role="status">
          Terrain Studio is disabled on this instance. Your saved projects remain available.
        </p>
      )}
      <ReleaseDialog
        open={creditsOpen}
        onClose={() => setCreditsOpen(false)}
        title="Terrain credits"
      >
        <CreditPanel domain="Terrain" available={wallet?.available} reserved={wallet?.reserved}>
          {' '}
          <p>
            Questions and brainstorming are free and require an available credit. A creation or
            revision request starts one build of up to 12 entries. Failed generations return their
            reservation.
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
        </CreditPanel>
      </ReleaseDialog>
      {!id ? (
        <NewStudio
          title="Your terrain"
          value={text}
          onChange={setText}
          onSend={() => void action(create)}
          disabledReason={
            busy
              ? 'Saving your project…'
              : !wallet?.enabled
                ? 'Generation is unavailable.'
                : !wallet.available
                  ? 'An available Terrain credit is needed to chat or build.'
                  : undefined
          }
          onBlocked={
            !busy && wallet?.enabled && !wallet.available ? () => setCreditsOpen(true) : undefined
          }
          pricing="Discussion spends no credits; 1 available Terrain credit is required. Creation requests build automatically · 1 credit on delivery."
          projects={
            <nav aria-label="Terrain projects">
              {projects.map((p) => (
                <Link key={p.id} to={'/terrain-studio/' + p.id}>
                  {p.title}
                </Link>
              ))}
            </nav>
          }
          tools={
            <label>
              Project title{' '}
              <input value={title} onChange={(e) => setTitle(e.target.value)} maxLength={128} />
            </label>
          }
        />
      ) : (
        <StudioWorkspace
          focusChat={focusChat}
          attention={pending ? 'Working' : undefined}
          result={
            !pending &&
            latestGeneration &&
            (latestGeneration.status === 'ready' || latestGeneration.status === 'failed')
              ? {
                  id: `${latestGeneration.id}:${latestGeneration.status}`,
                  status: latestGeneration.status,
                  text:
                    latestGeneration.status === 'ready'
                      ? 'Terrain creation ready in Preview.'
                      : 'Terrain creation needs attention in Preview.',
                }
              : undefined
          }
          conversation={
            <aside className="ts-chat">
              <h2>Design conversation</h2>
              <ConversationPane
                label="Terrain design conversation"
                count={thread?.messages.length ?? 0}
                firstMessageId={thread?.messages[0]?.id}
              >
                {thread?.messages.map((m) => (
                  <article key={m.id} className={'ts-message ' + m.role}>
                    <strong>{m.role === 'user' ? 'You' : 'Terrain designer'}</strong>
                    <p>{m.text}</p>
                  </article>
                ))}
              </ConversationPane>
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  void action(send);
                }}
              >
                <label>
                  Describe your creation or ask a question
                  <p className="studio-edit-target">
                    Editing current saved draft · {draft?.revision}
                  </p>
                  <ChatInput
                    rows={5}
                    maxLength={8000}
                    value={text}
                    disabled={retrying}
                    onChange={(e) => setText(e.target.value)}
                    placeholder="Create a fungal swamp with slow marsh, glowing wood trees, and renewable mushroom food."
                    onSend={() => {
                      if (!busy && !pending && wallet?.enabled && text.trim() && draft && !dirty)
                        void action(send);
                    }}
                  />
                </label>
                <details className="studio-attachments">
                  <summary>
                    <Icon name="upload" size={18} /> Reference images
                  </summary>{' '}
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
                  <div className="ts-references">
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
                </details>{' '}
                <button
                  className="primary"
                  disabled={
                    busy || !!pending || dirty || !wallet?.enabled || !text.trim() || !draft
                  }
                >
                  <Icon name={retrying ? 'refresh' : 'send'} size={18} />{' '}
                  {retrying ? 'Retry saved request' : 'Send'}
                </button>
                {retrying && (
                  <button
                    type="button"
                    disabled={busy}
                    onClick={() => {
                      submission.current = null;
                      setRetrying(false);
                      studioSession.removeItem(`terrain-studio-turn:${account.id}:${id}`);
                    }}
                  >
                    Discard local retry
                  </button>
                )}
                {dirty && <p>Save your manual edits before asking AI to revise the set.</p>}
                <p className="studio-compose-help">
                  Enter to send · Shift + Enter for a new line. Creation requests build
                  automatically · 1 credit on delivery.
                </p>
                {!wallet?.available && (
                  <p>An available Terrain credit is needed to chat or build.</p>
                )}
              </form>
              {pending && (
                <div role="status">
                  <p>
                    {pending.status === 'uncertain'
                      ? 'The provider outcome needs reconciliation. No duplicate call will be sent.'
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
                    Cancel
                  </button>
                  {cancellationReason(pending.status) && (
                    <p>{cancellationReason(pending.status)}</p>
                  )}
                </div>
              )}
            </aside>
          }
          artifact={
            <>
              {dirty && (
                <p role="status">
                  Save your manual terrain edits in Edit before generating, restoring a version, or
                  undoing.
                </p>
              )}
              {undo.delivered !== undefined && (
                <div className="studio-revisions" role="status">
                  <span>Generated edits applied to revision {undo.delivered}.</span>
                  <button
                    aria-disabled={!undo.canUndo || dirty || !!pending}
                    onClick={() => {
                      if (!undo.canUndo || dirty || pending || !undo.before || !draft) return;
                      const previous = undo.before;
                      void action(async () => {
                        await request(
                          'POST',
                          `${ROOT}/threads/${id}/drafts/${previous.revision}/restore`,
                          { body: { expectedRevision: draft.revision } },
                        );
                        undo.clear();
                      });
                    }}
                  >
                    <Icon name="restore" size={18} /> Undo
                  </button>
                  {!undo.canUndo && (
                    <span>A newer draft prevents undo. Use history to restore it.</span>
                  )}
                </div>
              )}
              <StudioTabs
                label="Artifact view"
                panels={{
                  preview: 'studio-panel-artifact-view-preview',
                  edit: 'studio-panel-artifact-view-edit',
                  history: 'studio-panel-artifact-view-history',
                }}
                value={view}
                onChange={(next) => {
                  if (
                    dirty &&
                    next !== 'edit' &&
                    !window.confirm('Leave the editor with unsaved changes?')
                  )
                    return;
                  setView(next);
                }}
                items={[
                  { id: 'preview', label: 'Preview', icon: 'eye' },
                  { id: 'edit', label: 'Edit', icon: 'pencil' },
                  { id: 'history', label: 'History', icon: 'restore' },
                ]}
              />
              <div
                id="studio-panel-artifact-view-preview"
                role="tabpanel"
                aria-labelledby="studio-tab-artifact-view-preview"
                hidden={view !== 'preview'}
              >
                {' '}
                <div className="ts-preview">
                  <h2>Your set</h2>
                  <RevisionControls
                    viewed={viewedRevision?.title ?? 'current saved draft'}
                    target="current saved draft"
                    follow={inspected ? () => setInspected('') : undefined}
                  />
                  {inspected && !shownDraft && <p role="status">Loading the inspected version…</p>}
                  {viewedRevision && (
                    <ValidationSummary version={viewedRevision.title}>
                      <p>
                        {viewedRevision.report.valid
                          ? 'Definition validation passed.'
                          : 'Definition validation failed.'}
                      </p>
                    </ValidationSummary>
                  )}
                  {shownProgress && (
                    <>
                      <ol className="ts-stages">
                        {shownProgress.stages.map((s) => (
                          <li key={s.id} data-status={s.status}>
                            <strong>{s.label}</strong>
                            <span>{s.status}</span>
                            {s.detail && <small>{s.detail}</small>}
                          </li>
                        ))}
                      </ol>
                      {shownProgress.notes.slice(-3).map((n, i) => (
                        <p role="status" key={i}>
                          {n.text}
                        </p>
                      ))}
                      {shownProgress.checks.map((c) => (
                        <p key={c.id}>
                          {c.label}: {c.status} · {c.detail}
                        </p>
                      ))}
                    </>
                  )}
                  {thread?.requests.find((r) => r.id === shownProgress?.requestId)?.error && (
                    <p role="alert">
                      {thread.requests.find((r) => r.id === shownProgress?.requestId)?.error}
                    </p>
                  )}
                  {shownDraft && (
                    <>
                      <p>
                        {shownDraft.package.terrains.length} terrains ·{' '}
                        {shownDraft.package.resources.length} resources · revision{' '}
                        {shownDraft.revision}
                      </p>
                      <p>
                        Validated definitions are importable. Review gameplay on your own maps
                        before relying on balance.
                      </p>
                      {shownProgress?.artifacts
                        .filter((a) => a.kind === 'preview')
                        .map((a) => (
                          <figure key={a.id}>
                            <img className="set-contact" src={a.url} alt={a.label} />
                            <figcaption>{a.label} · saved generation preview</figcaption>
                          </figure>
                        ))}
                      <SetPreview pack={shownDraft.package} gallery />
                      <div className="ts-entries">
                        {[...shownDraft.package.terrains, ...shownDraft.package.resources].map(
                          (e) => (
                            <article key={String(e['key'])}>
                              <h3>
                                {String(
                                  e['name'] ??
                                    (e['presentation'] as { name?: string })?.name ??
                                    e['key'],
                                )}
                              </h3>
                              <dl>
                                {Object.entries((e['properties'] as object) ?? {}).map(([k, v]) => (
                                  <div key={k}>
                                    <dt>{propertyLabel(k)}</dt>
                                    <dd>
                                      {typeof v === 'number' && k.endsWith('HealthQ8')
                                        ? `${v / 256} HP per exposed tick`
                                        : typeof v === 'number' && k.endsWith('Q8')
                                          ? `${v / 256}×`
                                          : typeof v === 'number' &&
                                              ['growthRate', 'spreadRate'].includes(k)
                                            ? `${(v / (k === 'growthRate' ? 65536 : 196608)).toFixed(2)}× wheat`
                                            : typeof v === 'boolean'
                                              ? v
                                                ? 'Yes'
                                                : 'No'
                                              : String(v)}
                                    </dd>
                                  </div>
                                ))}
                              </dl>
                              {!!e['yields'] && (
                                <ul>
                                  {Object.entries(
                                    e['yields'] as Record<
                                      string,
                                      { capacity: number; initial: number; consumption: string }
                                    >,
                                  ).map(([material, yielding]) => (
                                    <li key={material}>
                                      {propertyLabel(material)}: starts at {yielding.initial}, holds{' '}
                                      {yielding.capacity};{' '}
                                      {yielding.consumption === 'one'
                                        ? 'one unit per harvest'
                                        : yielding.consumption === 'infinite'
                                          ? 'stock remains available after harvesting'
                                          : 'the whole stock per harvest'}
                                    </li>
                                  ))}
                                </ul>
                              )}
                            </article>
                          ),
                        )}
                      </div>
                      <button onClick={() => setView('edit')}>
                        <Icon name="pencil" size={18} /> Edit & release settings
                      </button>
                    </>
                  )}
                </div>
              </div>
              <div
                id="studio-panel-artifact-view-edit"
                role="tabpanel"
                aria-labelledby="studio-tab-artifact-view-edit"
                hidden={view !== 'edit'}
              >
                <fieldset disabled={!!pending}>
                  {draft && (
                    <SetEditor
                      serverRevision={draft.revision}
                      id={draft.id}
                      key={inspectorRevision}
                      onDirtyChange={manualDirty}
                      onSaved={() => {
                        manualDirty(false);
                        void refresh();
                      }}
                    />
                  )}
                </fieldset>
              </div>
              <div
                id="studio-panel-artifact-view-history"
                role="tabpanel"
                aria-labelledby="studio-tab-artifact-view-history"
                hidden={view !== 'history'}
              >
                <section aria-label="Saved draft history">
                  <h2>Previous saved drafts</h2>
                  {thread?.draftHistory?.map((d) => (
                    <article key={d.revision}>
                      <h3>{d.title}</h3>
                      <p>Saved draft · validation remains version-specific.</p>
                      <a href={`${ROOT}/threads/${id}/drafts/${d.revision}/file`}>
                        Download saved draft
                      </a>
                      <button
                        disabled={busy || !!pending || dirty}
                        onClick={() =>
                          void action(async () => {
                            if (
                              !draft ||
                              !window.confirm(
                                'Restore this saved draft? The current draft will remain in history.',
                              )
                            )
                              return;
                            await request(
                              'POST',
                              `${ROOT}/threads/${id}/drafts/${d.revision}/restore`,
                              { body: { expectedRevision: draft.revision } },
                            );
                            setInspected('');
                            setFocusChat((n) => n + 1);
                          })
                        }
                      >
                        Edit this saved draft
                      </button>
                    </article>
                  ))}
                </section>{' '}
                {thread?.revisions.length !== 0 && (
                  <details>
                    <summary>Generated revisions</summary>
                    {thread?.revisions.map((r) => (
                      <article key={r.requestId}>
                        <h3>{r.title}</h3>
                        <button
                          onClick={() => {
                            setInspected(r.requestId);
                            setView('preview');
                          }}
                        >
                          Inspect this version
                        </button>
                        <p>
                          {r.applied ? 'Applied to the draft' : 'Saved candidate'} · based on
                          revision {r.baseRevision}
                        </p>
                        <a href={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`}>
                          Download candidate
                        </a>
                        <CandidatePreview
                          url={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`}
                        />
                        {draft && (
                          <button
                            disabled={busy || !!pending || dirty || !!draft.publishedVersionId}
                            onClick={() =>
                              void action(async () => {
                                if (
                                  !window.confirm(
                                    'Restore this version as the current draft? The current draft stays in history.',
                                  )
                                )
                                  return;
                                await request(
                                  'POST',
                                  `${ROOT}/threads/${id}/requests/${r.requestId}/adopt`,
                                  { body: { expectedRevision: draft.revision } },
                                );
                                setInspected('');
                                setFocusChat((n) => n + 1);
                              })
                            }
                          >
                            Edit this version
                          </button>
                        )}
                      </article>
                    ))}
                  </details>
                )}
              </div>
            </>
          }
        />
      )}
    </StudioShell>
  );
}

function CandidatePreview({ url }: { url: string }) {
  const [pack, setPack] = useState<SetPackage>();
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  return pack ? (
    <SetPreview pack={pack} gallery />
  ) : (
    <>
      <button
        disabled={busy}
        onClick={() => {
          setBusy(true);
          void request<SetPackage>('GET', url)
            .then(setPack)
            .catch((e) => setError(String(e)))
            .finally(() => setBusy(false));
        }}
      >
        Inspect candidate
      </button>
      {error && <p role="alert">{error}</p>}
    </>
  );
}

function propertyLabel(key: string) {
  const labels: Record<string, string> = {
    groundSpeedQ8: 'Ground movement',
    groundHealthQ8: 'Ground health change',
    airHealthQ8: 'Air health change',
    swimSpeedQ8: 'Swimming movement',
    growthQ8: 'Resource growth',
    growthRate: 'Growth rate',
    spreadRate: 'Spread rate',
    primaryMaterial: 'Main material',
    farmable: 'Can be farmed',
    passable: 'Units can cross',
    buildable: 'Buildings allowed',
  };
  return (
    labels[key] ??
    key
      .replace(/Q8$/, '')
      .replace(/([a-z])([A-Z])/g, '$1 $2')
      .replace(/^./, (c) => c.toUpperCase())
  );
}
