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
import { SetWorkspace } from '../sets/Workspace.tsx';
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
  const { account } = useSession(),
    { navigate, location } = useRouter();
  const [wallet, setWallet] = useState<Wallet>(),
    [projects, setProjects] = useState<{ id: string; title: string }[]>([]),
    [thread, setThread] = useState<TerrainStudioThread>(),
    [draft, setDraft] = useState<SetDraft>(),
    [progress, setProgress] = useState<TerrainStudioProgress>(),
    [text, setText] = useState(''),
    [title, setTitle] = useState('New terrain set'),
    [error, setError] = useState(''),
    [connection, setConnection] = useState(''),
    [busy, setBusy] = useState(false),
    [dirty, setDirty] = useState(false),
    [selectedRefs, setSelectedRefs] = useState<string[]>([]);
  const [retrying, setRetrying] = useState(false);
  const [inspectorRevision, setInspectorRevision] = useState<number>();
  const dirtyRef = useRef(false),
    messages = useRef<HTMLDivElement>(null),
    followMessages = useRef(true);
  const manualDirty = useCallback((value: boolean) => {
    dirtyRef.current = value;
    setDirty(value);
  }, []);
  const submission = useRef<TerrainStudioTurn | null>(null),
    creation = useRef(crypto.randomUUID()),
    serial = useRef(0);
  const pending = thread?.requests.findLast((r) => !['ready', 'failed'].includes(r.status));
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
        sessionStorage.removeItem(key);
        setText('');
      }
    },
    [id, account],
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
      const value = sessionStorage.getItem(key);
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
      sessionStorage.removeItem(key);
    }
  }, [account, id]);
  useEffect(() => {
    if (followMessages.current && messages.current)
      messages.current.scrollTop = messages.current.scrollHeight;
  }, [thread?.messages]);
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
  async function create() {
    const result = await request<{ id: string }>('POST', ROOT + '/threads', {
      body: {
        id: creation.current,
        title,
        ...(location.search.get('draft') ? { draftId: location.search.get('draft') } : {}),
        ...(location.search.get('version') ? { versionId: location.search.get('version') } : {}),
      },
    });
    navigate('/terrain-studio/' + result.id);
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
    sessionStorage.setItem(`terrain-studio-turn:${account.id}:${id}`, JSON.stringify(value));
    followMessages.current = true;
    try {
      await request('POST', `${ROOT}/threads/${id}/turns`, { body: value });
    } catch (e) {
      // A definite rejection did not accept this turn. Keep its prompt editable;
      // transport/server failures retain the UUID to safely retry an unknown outcome.
      if (e instanceof ApiError && e.status >= 400 && e.status < 500 && e.status !== 408) {
        submission.current = null;
        setRetrying(false);
        sessionStorage.removeItem(`terrain-studio-turn:${account.id}:${id}`);
      }
      throw e;
    }
    submission.current = null;
    setRetrying(false);
    sessionStorage.removeItem(`terrain-studio-turn:${account.id}:${id}`);
    setText('');
  }
  if (!account)
    return (
      <section>
        <h1>AI Terrain Studio</h1>
        <p>Describe a world. Create its terrain and resources.</p>
        <a href="/signin">Sign in to create a set</a>
      </section>
    );
  return (
    <section className="terrain-studio">
      <header className="ts-header">
        <div>
          <Link to="/sets">Terrain & resources</Link>
          <h1>{thread?.title ?? 'AI Terrain Studio'}</h1>
          <p>Create a world’s terrain, resources, and the way they behave.</p>
        </div>
        <div>
          <strong>{wallet?.available ?? 0} credits available</strong>
          <p>{wallet?.reserved ?? 0} reserved · 1 credit per delivered generation</p>
        </div>
      </header>
      {error && <p role="alert">{error}</p>}
      {connection && <p role="status">{connection}</p>}
      {wallet && !wallet.enabled && (
        <p role="status">
          Terrain Studio is disabled on this instance. Your saved projects remain available.
        </p>
      )}
      <details className="ts-credits">
        <summary>Terrain credits</summary>
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
      </details>
      {!id ? (
        <>
          <div className="ts-start">
            <h2>
              {location.search.has('draft')
                ? 'Edit your set with AI'
                : location.search.has('version')
                  ? 'Remix this set'
                  : 'What world will you create?'}
            </h2>
            <p>
              Try a fungal swamp, a windswept desert, or a winter orchard. Describe how the ground
              and resources should work as well as how they look.
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
          <div className="ts-projects">
            {projects.map((p) => (
              <Link className="card" key={p.id} to={'/terrain-studio/' + p.id}>
                {p.title}
              </Link>
            ))}
          </div>
        </>
      ) : (
        <div className="ts-layout">
          <aside className="ts-chat">
            <h2>Design conversation</h2>
            <div
              className="ts-messages"
              aria-live="polite"
              ref={messages}
              onScroll={() => {
                const box = messages.current;
                if (box)
                  followMessages.current = box.scrollHeight - box.scrollTop - box.clientHeight < 48;
              }}
            >
              {thread?.messages.map((m) => (
                <article key={m.id} className={'ts-message ' + m.role}>
                  <strong>{m.role === 'user' ? 'You' : 'Terrain designer'}</strong>
                  <p>{m.text}</p>
                </article>
              ))}
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
                  placeholder="Create a fungal swamp with slow marsh, glowing wood trees, and renewable mushroom food."
                />
              </label>
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
              <button
                className="primary"
                disabled={
                  busy ||
                  !!pending ||
                  dirty ||
                  !wallet?.enabled ||
                  !wallet.available ||
                  !text.trim() ||
                  !draft
                }
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
                    sessionStorage.removeItem(`terrain-studio-turn:${account.id}:${id}`);
                  }}
                >
                  Discard local retry
                </button>
              )}
              {dirty && <p>Save your manual edits before asking AI to revise the set.</p>}
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
                      await request('POST', `${ROOT}/threads/${id}/requests/${pending.id}/cancel`, {
                        body: {},
                      });
                    })
                  }
                >
                  Cancel
                </button>
              </div>
            )}
          </aside>
          <div className="ts-preview">
            <h2>Your set</h2>
            {progress && (
              <>
                <ol className="ts-stages">
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
            {thread?.requests.at(-1)?.error && <p role="alert">{thread.requests.at(-1)?.error}</p>}
            {draft && (
              <>
                <p>
                  {draft.package.terrains.length} terrains · {draft.package.resources.length}{' '}
                  resources · revision {draft.revision}
                </p>
                <p>
                  Validated definitions are importable. Review gameplay on your own maps before
                  relying on balance.
                </p>
                {progress?.artifacts
                  .filter((a) => a.kind === 'preview')
                  .map((a) => (
                    <figure key={a.id}>
                      <img className="set-contact" src={a.url} alt={a.label} />
                      <figcaption>{a.label} · saved generation preview</figcaption>
                    </figure>
                  ))}
                <SetPreview pack={draft.package} gallery />
                <div className="ts-entries">
                  {[...draft.package.terrains, ...draft.package.resources].map((e) => (
                    <article key={String(e['key'])}>
                      <h3>
                        {String(
                          e['name'] ?? (e['presentation'] as { name?: string })?.name ?? e['key'],
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
                  ))}
                </div>
                <details className="ts-inspector">
                  <summary>Manually edit entries, artwork, and release settings</summary>
                  {dirty && inspectorRevision !== draft.revision && (
                    <div role="alert">
                      <p>
                        Another session saved revision {draft.revision}. Your unsaved edits are
                        preserved, but saving them may conflict. Copy any edits you want to keep
                        before loading the saved revision.
                      </p>
                      <button
                        type="button"
                        onClick={() => {
                          manualDirty(false);
                          setInspectorRevision(draft.revision);
                        }}
                      >
                        Discard manual edits and load saved revision
                      </button>
                    </div>
                  )}
                  <fieldset disabled={!!pending}>
                    <SetWorkspace
                      id={draft.id}
                      key={inspectorRevision}
                      onDirtyChange={manualDirty}
                      onSaved={() => {
                        manualDirty(false);
                        void refresh();
                      }}
                    />
                  </fieldset>
                </details>
                <Link className="btn" to={'/sets/drafts/' + draft.id}>
                  Open set workspace & publish
                </Link>
              </>
            )}
            {thread?.revisions.length !== 0 && (
              <details>
                <summary>Generated revisions</summary>
                {thread?.revisions.map((r) => (
                  <article key={r.requestId}>
                    <h3>{r.title}</h3>
                    <p>
                      {r.applied ? 'Applied to the draft' : 'Saved candidate'} · based on revision{' '}
                      {r.baseRevision}
                    </p>
                    <a href={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`}>
                      Download candidate
                    </a>
                    <CandidatePreview url={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`} />
                    {draft && (
                      <button
                        disabled={busy || !!pending || dirty || !!draft.publishedVersionId}
                        onClick={() =>
                          void action(async () => {
                            await request(
                              'POST',
                              `${ROOT}/threads/${id}/requests/${r.requestId}/adopt`,
                              { body: { expectedRevision: draft.revision } },
                            );
                          })
                        }
                      >
                        Adopt this revision
                      </button>
                    )}
                  </article>
                ))}
              </details>
            )}
          </div>
        </div>
      )}
    </section>
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
