import { useEffect, useState } from 'react';
import type { StudioThread, StudioSettings, StudioRequest } from '@glob2/protocol';
import { api, ApiError, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
interface Wallet {
  enabled: boolean;
  available: number;
  reserved: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
  usage: { id: string; kind: string; amount: string; created_at: string }[];
}
const ROOT = '/api/v1/map-studio';
export function MapStudio({ id }: { id?: string }) {
  const { account } = useSession();
  if (account === undefined) return <p>Loading your account…</p>;
  if (account?.kind !== 'registered')
    return (
      <section>
        <h1>AI Map Studio</h1>
        <p>Sign in with a registered account to buy map credits and design maps.</p>
        <a className="btn primary" href="/signin">
          Sign in
        </a>
      </section>
    );
  return <RegisteredStudio key={`${account.id}:${id ?? ''}`} id={id} />;
}
type Delivered = StudioRequest & {
  map_id: string;
  map_hash: string;
  input: StudioRequest['input'] & { settings: StudioSettings };
};
function RegisteredStudio({ id }: { id?: string }) {
  const { account } = useSession(),
    { navigate } = useRouter();
  const [wallet, setWallet] = useState<Wallet>(),
    [threads, setThreads] = useState<{ id: string; title: string }[]>([]),
    [thread, setThread] = useState<StudioThread>();
  const [title, setTitle] = useState(''),
    [draft, setDraft] = useState(() =>
      account && id ? (sessionStorage.getItem(`studio-draft:${account.id}:${id}`) ?? '') : '',
    ),
    [error, setError] = useState(''),
    [connectionError, setConnectionError] = useState(''),
    [busy, setBusy] = useState(false);
  const [settings, setSettings] = useState<StudioSettings>({ width: 256, height: 256, players: 4 }),
    [parent, setParent] = useState<string>(),
    [comparison, setComparison] = useState<string[]>([]);
  const [retry, setRetry] = useState<{ path: string; body: unknown }>();
  const draftKey = account && id ? `studio-draft:${account.id}:${id}` : undefined;
  useEffect(() => {
    if (draftKey) sessionStorage.setItem(draftKey, draft);
  }, [draft, draftKey]);
  useEffect(() => {
    if (account?.kind !== 'registered') return;
    const abort = new AbortController();
    const refresh = () => {
      void Promise.all([
        request<Wallet>('GET', ROOT + '/account', { signal: abort.signal }),
        request<{ items: { id: string; title: string }[] }>('GET', ROOT + '/threads', {
          signal: abort.signal,
        }),
      ])
        .then(([w, t]) => {
          setWallet(w);
          setThreads(t.items);
        })
        .catch((e: unknown) => {
          if (!abort.signal.aborted) setError(message(e));
        });
    };
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => {
      abort.abort();
      clearInterval(timer);
    };
  }, [account]);
  useEffect(() => {
    if (!id || account?.kind !== 'registered') return;
    const abort = new AbortController();
    async function update() {
      let first = true;
      while (!abort.signal.aborted) {
        try {
          const value = await request<StudioThread>(
            'GET',
            `${ROOT}/threads/${id}${first ? '' : '/updates'}`,
            { signal: abort.signal },
          );
          if (abort.signal.aborted) return;
          setThread((current) => mergeThread(current, value));
          setConnectionError('');
          first = false;
        } catch {
          if (abort.signal.aborted) return;
          setConnectionError('Connection interrupted. Reconnecting to your saved thread…');
          first = true;
          await new Promise<void>((resolve) => {
            const done = () => {
              clearTimeout(timer);
              abort.signal.removeEventListener('abort', done);
              resolve();
            };
            const timer = setTimeout(done, 5000);
            abort.signal.addEventListener('abort', done, { once: true });
          });
        }
      }
    }
    void update();
    return () => abort.abort();
  }, [id, account]);
  const active = thread?.requests.find((r) => !['ready', 'failed'].includes(r.status));
  const versions =
    thread?.requests.filter(
      (r): r is Delivered =>
        r.kind === 'generate' &&
        r.status === 'ready' &&
        !!r.map_id &&
        !!r.map_hash &&
        !!r.input.settings,
    ) ?? [];
  const selected = versions.find((v) => v.id === parent);
  async function action(work: () => Promise<void>) {
    setBusy(true);
    setError('');
    try {
      await work();
    } catch (e) {
      setError(message(e));
    } finally {
      setBusy(false);
    }
  }
  async function send(path: string, body: unknown) {
    setRetry({ path, body });
    await request('POST', path, { body });
    setRetry(undefined);
    if (id) {
      const value = await request<StudioThread>('GET', `${ROOT}/threads/${id}`);
      setThread((current) => mergeThread(current, value));
    }
  }
  function revise(v: Delivered) {
    setParent(v.id);
    setSettings(v.input.settings);
  }
  function changeSettings(next: StudioSettings) {
    setSettings(next);
    setParent(undefined);
  }
  function preview(v: StudioRequest) {
    return `/api/v1/maps/${v.map_id}/versions/${v.map_hash}/preview.png`;
  }
  async function publish(v: Delivered) {
    await api.updateMap(v.map_id, { visibility: 'public' });
    navigate(`/maps/${v.map_id}`);
  }
  async function host(v: Delivered) {
    const result = await request<{ code: string }>(
      'POST',
      `${ROOT}/threads/${id}/versions/${v.id}/room`,
      { body: {} },
    );
    window.location.assign(`/play/?join=${encodeURIComponent(result.code)}`);
  }
  if (account === undefined) return <p>Loading your account…</p>;
  if (account?.kind !== 'registered')
    return (
      <section>
        <h1>AI Map Studio</h1>
        <p>Sign in with a registered account to buy map credits and design maps.</p>
        <a className="btn primary" href="/signin">
          Sign in
        </a>
      </section>
    );
  return (
    <section className="map-studio">
      <header>
        <h1>AI Map Studio</h1>
        <p>Describe a landscape. Refine it together. Play your creation.</p>
      </header>
      {connectionError && <p role="status">{connectionError}</p>}
      {error && (
        <div role="alert" className="notice">
          {error}
          {retry && (
            <button
              disabled={busy}
              onClick={() =>
                void action(async () => {
                  const pending = retry;
                  if (!pending) return;
                  await send(pending.path, pending.body);
                })
              }
            >
              Retry the same request
            </button>
          )}
        </div>
      )}
      {wallet && (
        <div className="studio-wallet">
          <strong>{wallet.available} map credits available</strong>
          <span>{wallet.reserved} reserved</span>
          <p>
            Discussion is included. Each delivered map or revision costs 1 credit. Failed
            generations return the credit.
          </p>
          {!wallet.enabled && <p>AI Map Studio is not enabled on this instance.</p>}
          {wallet.packs.map((pack) => (
            <button
              key={pack.id}
              disabled={busy}
              onClick={() =>
                void action(async () => {
                  const result = await request<{ url: string }>('POST', ROOT + '/checkout', {
                    body: { pack: pack.id },
                  });
                  const url = new URL(result.url);
                  if (url.protocol !== 'https:' || url.hostname !== 'checkout.stripe.com')
                    throw new Error('Invalid checkout destination.');
                  window.location.assign(url.href);
                })
              }
            >
              Buy {pack.credits} credits ·{' '}
              {new Intl.NumberFormat(undefined, {
                style: 'currency',
                currency: pack.currency,
              }).format(pack.amount / 100)}
            </button>
          ))}
          {!wallet.packs.length && <p>Credit purchases are currently unavailable.</p>}
          <details>
            <summary>Credit activity</summary>
            <ul>
              {wallet.usage.map((entry) => (
                <li key={entry.id}>
                  {new Date(entry.created_at).toLocaleString()} · {entry.kind} · {entry.amount}{' '}
                  credits
                </li>
              ))}
            </ul>
          </details>
        </div>
      )}
      <div className="studio-layout">
        <aside aria-label="Map threads">
          <h2>Your map threads</h2>
          <ul>
            {threads.map((t) => (
              <li key={t.id}>
                <Link to={`/map-studio/${t.id}`}>{t.title}</Link>
              </li>
            ))}
          </ul>
          <form
            onSubmit={(e) => {
              e.preventDefault();
              void action(async () => {
                const t = await request<{ id: string }>('POST', ROOT + '/threads', {
                  body: { title: title.trim() || 'New map' },
                });
                setTitle('');
                navigate(`/map-studio/${t.id}`);
              });
            }}
          >
            <label className="field">
              New thread title
              <input value={title} maxLength={128} onChange={(e) => setTitle(e.target.value)} />
            </label>
            <button disabled={busy || !wallet?.enabled}>New map thread</button>
          </form>
        </aside>
        <div>
          {!id ? (
            <p>Open a thread or create one to start designing.</p>
          ) : !thread ? (
            <p>Loading your map thread…</p>
          ) : (
            <>
              <h2>{thread.title}</h2>
              {(thread.history?.messagesBefore || thread.history?.requestsBefore) && (
                <button
                  disabled={busy}
                  onClick={() =>
                    void action(async () => {
                      const query = new URLSearchParams();
                      if (thread.history?.messagesBefore)
                        query.set('messagesBefore', thread.history.messagesBefore);
                      if (thread.history?.requestsBefore)
                        query.set('requestsBefore', thread.history.requestsBefore);
                      const older = await request<StudioThread>(
                        'GET',
                        `${ROOT}/threads/${id}?${query}`,
                      );
                      setThread((current) => ({
                        ...mergeThread(current, older),
                        history: older.history,
                      }));
                    })
                  }
                >
                  Load earlier conversation and versions
                </button>
              )}
              <div
                className="studio-chat"
                role="log"
                aria-label="Map design conversation"
                aria-live="polite"
              >
                {thread.messages.map((m) => (
                  <article key={m.id} className={`studio-message ${m.role}`}>
                    <strong>{m.role === 'user' ? 'You' : 'Map designer'}</strong>
                    <p>{m.text}</p>
                  </article>
                ))}
              </div>
              {active && (
                <p role="status">
                  {active.status === 'uncertain'
                    ? 'The provider outcome needs reconciliation. Your credit remains reserved.'
                    : active.error
                      ? active.error
                      : active.kind === 'chat'
                        ? 'The map designer is replying…'
                        : `Generating your map: ${active.status}…`}
                </p>
              )}
              {thread.requests
                .filter((r) => r.status === 'failed')
                .map((r) => (
                  <p className="notice" key={r.id}>
                    {r.error ?? 'The request failed.'}
                  </p>
                ))}
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  if (!draft.trim()) return;
                  void action(async () => {
                    const text = draft;
                    await send(`${ROOT}/threads/${id}/messages`, { id: crypto.randomUUID(), text });
                    setDraft('');
                  });
                }}
              >
                <label className="field">
                  Describe your map or discuss changes
                  <textarea
                    rows={4}
                    value={draft}
                    maxLength={8000}
                    onChange={(e) => {
                      setDraft(e.target.value);
                      setRetry(undefined);
                    }}
                  />
                </label>
                <button
                  disabled={
                    busy || !!active || !wallet?.enabled || !wallet.available || !draft.trim()
                  }
                >
                  Send message
                </button>
              </form>
              <fieldset disabled={busy || !!active}>
                <legend>Next map</legend>
                <div className="studio-controls">
                  {(['width', 'height'] as const).map((axis) => (
                    <label key={axis}>
                      {axis === 'width' ? 'Width' : 'Height'}
                      <select
                        value={settings[axis]}
                        onChange={(e) =>
                          changeSettings({
                            ...settings,
                            [axis]: Number(e.target.value) as StudioSettings['width'],
                          })
                        }
                      >
                        {[128, 256, 512].map((n) => (
                          <option key={n} value={n}>
                            {n} cells
                          </option>
                        ))}
                      </select>
                    </label>
                  ))}
                  <label>
                    Players
                    <select
                      value={settings.players}
                      onChange={(e) =>
                        changeSettings({ ...settings, players: Number(e.target.value) })
                      }
                    >
                      {[2, 3, 4, 5, 6, 7, 8].map((n) => (
                        <option key={n}>{n}</option>
                      ))}
                    </select>
                  </label>
                </div>
                <p>
                  {selected
                    ? `Revising version ${versions.indexOf(selected) + 1}.`
                    : 'Creating a fresh map from this discussion.'}{' '}
                  {selected && (
                    <button type="button" onClick={() => setParent(undefined)}>
                      Start fresh
                    </button>
                  )}
                </p>
                <button
                  className="primary"
                  disabled={
                    !wallet?.enabled ||
                    !wallet.available ||
                    !thread.messages.length ||
                    !!draft.trim()
                  }
                  onClick={() =>
                    void action(() =>
                      send(`${ROOT}/threads/${id}/generate`, {
                        id: crypto.randomUUID(),
                        settings,
                        ...(parent ? { parent } : {}),
                      }),
                    )
                  }
                >
                  Generate — 1 credit
                </button>
                {draft.trim() && <p>Send your draft message before generating.</p>}
              </fieldset>
              <h2>Map versions</h2>
              <p>
                Drafts are private. Publishing shares only that version. Hosting a room shares its
                map with players in the room.
              </p>
              <div className="studio-gallery">
                {versions.map((v, i) => (
                  <article className="studio-version" key={v.id}>
                    <h3>
                      Version {i + 1}
                      {v.input.parent
                        ? ` · revised from version ${versions.findIndex((p) => p.id === v.input.parent) + 1}`
                        : ''}
                    </h3>
                    <img
                      src={preview(v)}
                      alt={`Imported map version ${i + 1}, ${v.input.settings.players} players`}
                      loading="lazy"
                    />
                    <p>
                      {v.input.settings.width}×{v.input.settings.height} ·{' '}
                      {v.input.settings.players} players
                    </p>
                    <div className="studio-actions">
                      <button disabled={busy || !!active} onClick={() => revise(v)}>
                        Revise this version
                      </button>
                      <a
                        className="btn"
                        href={`/api/v1/maps/${v.map_id}/versions/${v.map_hash}/file`}
                      >
                        Download
                      </a>
                      <button disabled={busy} onClick={() => void action(() => host(v))}>
                        Host room
                      </button>
                      <button disabled={busy} onClick={() => void action(() => publish(v))}>
                        Publish this version
                      </button>
                      <Link to={`/maps/${v.map_id}`}>Map details</Link>
                    </div>
                    <label>
                      <input
                        type="checkbox"
                        checked={comparison.includes(v.id)}
                        onChange={(e) =>
                          setComparison(
                            e.target.checked
                              ? [...comparison.slice(-1), v.id]
                              : comparison.filter((p) => p !== v.id),
                          )
                        }
                      />{' '}
                      Compare
                    </label>
                  </article>
                ))}
              </div>
              {comparison.length === 2 && (
                <section aria-label="Map comparison">
                  <h2>Compare versions</h2>
                  <div className="studio-gallery">
                    {comparison.map((version) => {
                      const v = versions.find((r) => r.id === version);
                      if (!v) return null;
                      return (
                        <figure key={version}>
                          <img src={preview(v)} alt={`Map version ${versions.indexOf(v) + 1}`} />
                          <figcaption>Version {versions.indexOf(v) + 1}</figcaption>
                        </figure>
                      );
                    })}
                  </div>
                </section>
              )}
            </>
          )}
        </div>
      </div>
    </section>
  );
}
function message(error: unknown) {
  return error instanceof ApiError
    ? error.message
    : error instanceof Error
      ? error.message
      : 'The request could not be completed.';
}

function mergeThread(current: StudioThread | undefined, next: StudioThread): StudioThread {
  if (!current || current.id !== next.id) return next;
  function merge<T extends { id: string; created_at: string }>(previous: T[], latest: T[]) {
    return [...new Map([...previous, ...latest].map((value) => [value.id, value])).values()].sort(
      (a, b) => a.created_at.localeCompare(b.created_at) || a.id.localeCompare(b.id),
    );
  }
  return {
    ...next,
    messages: merge(current.messages, next.messages),
    requests: merge(current.requests, next.requests),
    history: current.history ?? next.history,
  };
}
