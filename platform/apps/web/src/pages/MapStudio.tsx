import { useCallback, useEffect, useRef, useState } from 'react';
import { ApiError, api, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
import { MapStudioLanding } from './MapStudioLanding.tsx';
import { StudioWorkspace } from './studio/StudioWorkspace.tsx';
import { useStudioStream } from './studio/useStudioStream.ts';
import { useStudioDraft, type Pending } from './studio/useStudioDraft.ts';
import { CreditControls } from './studio/CreditControls.tsx';
import { ROOT, mergeThread, type Delivered, type Thread, type Wallet } from './studio/types.ts';
import '../styles/studio.css';

export function MapStudio({ id }: { id?: string }) {
  const { account } = useSession();
  if (account === undefined)
    return (
      <div className="ms-gate" role="status">
        Opening your studio…
      </div>
    );
  if (account?.kind !== 'registered')
    return (
      <div className="ms-gate">
        <span className="ms-eyebrow">AI MAP STUDIO</span>
        <h1>Your next world starts with an idea.</h1>
        <p>Sign in with a registered account to design, refine, and play your own maps.</p>
        <a className="btn primary" href="/signin">
          Sign in to create
        </a>
      </div>
    );
  return <RegisteredStudio key={`${account.id}:${id ?? 'new'}`} id={id} accountId={account.id} />;
}
function RegisteredStudio({ id, accountId }: { id?: string; accountId: string }) {
  const { navigate } = useRouter();
  const [wallet, setWallet] = useState<Wallet>();
  const [threads, setThreads] = useState<{ id: string; title: string }[]>([]);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [credits, setCredits] = useState(false);
  const [hidden, setHidden] = useState(document.hidden);
  useEffect(() => {
    const change = () => setHidden(document.hidden);
    document.addEventListener('visibilitychange', change);
    return () => document.removeEventListener('visibilitychange', change);
  }, []);
  const {
    draft,
    setDraft,
    pending,
    setPending,
    settings,
    setSettings,
    parent,
    fresh,
    setParent,
    draftKey: key,
  } = useStudioDraft(accountId, id);
  const previousDelivery = useRef<string | undefined>(undefined);
  const payment = new URLSearchParams(window.location.search).get('payment');
  const checkoutBalance = Number(
    sessionStorage.getItem(`studio-checkout-balance:${accountId}`) ?? 0,
  );
  const refreshWallet = useCallback(() => {
    void request<Wallet>('GET', `${ROOT}/account`)
      .then(setWallet)
      .catch((e) => setError(errorMessage(e)));
  }, []);
  const { thread, setThread, connection, revision, celebrate } = useStudioStream(id, refreshWallet);
  const latestDelivered = thread?.requests
    .filter(
      (r): r is Delivered =>
        r.kind === 'generate' &&
        r.status === 'ready' &&
        !!r.map_id &&
        !!r.map_hash &&
        !!r.input.settings,
    )
    .at(-1);
  useEffect(() => {
    if (!latestDelivered) {
      if (thread) previousDelivery.current = '';
      return;
    }
    if (
      (!fresh && !parent) ||
      (previousDelivery.current !== undefined && previousDelivery.current !== latestDelivered.id)
    ) {
      setParent(latestDelivered.id);
      setSettings(latestDelivered.input.settings);
    }
    previousDelivery.current = latestDelivered.id;
  }, [thread, latestDelivered, fresh, parent, setParent, setSettings]);
  useEffect(() => {
    const abort = new AbortController();
    void Promise.all([
      request<Wallet>('GET', `${ROOT}/account`, { signal: abort.signal }),
      request<{ items: { id: string; title: string }[] }>('GET', `${ROOT}/threads`, {
        signal: abort.signal,
      }),
    ])
      .then(([w, t]) => {
        setWallet(w);
        setThreads(t.items);
      })
      .catch((e) => {
        if (!abort.signal.aborted) setError(errorMessage(e));
      });
    const focus = () => refreshWallet();
    window.addEventListener('focus', focus);
    const timer = setInterval(refreshWallet, 15000);
    return () => {
      abort.abort();
      clearInterval(timer);
      window.removeEventListener('focus', focus);
    };
  }, [refreshWallet]);
  useEffect(() => {
    if (id) return;
    const returnProject = sessionStorage.getItem(`studio-checkout:${accountId}`);
    if (new URLSearchParams(window.location.search).has('payment') && returnProject) {
      sessionStorage.removeItem(`studio-checkout:${accountId}`);
      navigate(
        `/map-studio/${returnProject}?payment=${payment === 'cancelled' ? 'cancelled' : 'returned'}`,
        { replace: true },
      );
    } else if (wallet?.activeRequest)
      navigate(`/map-studio/${wallet.activeRequest.threadId}`, { replace: true });
  }, [id, wallet?.activeRequest, accountId, navigate, payment]);
  async function action(work: () => Promise<void>) {
    setBusy(true);
    setError('');
    try {
      await work();
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setBusy(false);
    }
  }
  async function submitPending(value: Pending) {
    setPending(value);
    try {
      await request('POST', value.path, { body: value.body });
    } catch (error) {
      // Only a definitive rejection releases the retry identity. Lost responses
      // and server failures may have committed and must reuse the same request.
      if (isRejectedSubmission(error)) setPending(undefined);
      throw error;
    }
    setPending(undefined);
    if (value.body.text)
      setDraft((current) => (current.trim() === value.body.text?.trim() ? '' : current));
    refreshWallet();
    if (id) {
      const value = await request<Thread>('GET', `${ROOT}/threads/${id}`);
      setThread((current) => mergeThread(current, value));
    }
  }
  function sendMessage() {
    void action(async () => {
      if (id) {
        await submitPending({
          path: `${ROOT}/threads/${id}/turns`,
          body: {
            id: crypto.randomUUID(),
            text: draft.trim(),
            settings,
            ...(parent ? { parent } : {}),
          },
        });
        return;
      }
      // Persist both identities before the first network call, including unknown outcomes.
      const createdKey = `studio-created:${accountId}`;
      type Creation = {
        id: string;
        title: string;
        messageId: string;
        text: string;
        settings: typeof settings;
      };
      const stored = sessionStorage.getItem(createdKey);
      const creation: Creation = stored
        ? (JSON.parse(stored) as Creation)
        : {
            id: crypto.randomUUID(),
            title: draft.trim().slice(0, 128),
            messageId: crypto.randomUUID(),
            text: draft.trim(),
            settings,
          };
      sessionStorage.setItem(createdKey, JSON.stringify(creation));
      try {
        await request<{ id: string }>('POST', `${ROOT}/threads`, {
          body: { id: creation.id, title: creation.title },
        });
      } catch (error) {
        if (isRejectedSubmission(error)) sessionStorage.removeItem(createdKey);
        throw error;
      }
      const created = creation.id;
      const pending: Pending = {
        path: `${ROOT}/threads/${created}/turns`,
        body: { id: creation.messageId, text: creation.text, settings: creation.settings },
      };
      sessionStorage.setItem(`studio-draft:${accountId}:${created}`, creation.text);
      sessionStorage.setItem(`studio-pending:${accountId}:${created}`, JSON.stringify(pending));
      sessionStorage.setItem(`studio-autosend:${accountId}:${created}`, '1');
      sessionStorage.setItem(
        `studio-settings:${accountId}:${created}`,
        JSON.stringify({ settings: creation.settings }),
      );
      if (sessionStorage.getItem(key)?.trim() === creation.text) sessionStorage.removeItem(key);
      sessionStorage.removeItem(createdKey);
      navigate(`/map-studio/${created}`);
    });
  }
  // Only the prompt-first navigation marks a request for automatic submission. Reloaded failures remain explicit retries.
  useEffect(() => {
    if (!id || !pending || !sessionStorage.getItem(`studio-autosend:${accountId}:${id}`)) return;
    sessionStorage.removeItem(`studio-autosend:${accountId}:${id}`);
    queueMicrotask(() => {
      void action(() => submitPending(pending));
    });
    // Submission belongs to this mounted project, not subsequent state changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  function buy(pack: string) {
    void action(async () => {
      if (id) sessionStorage.setItem(`studio-checkout:${accountId}`, id);
      else sessionStorage.removeItem(`studio-checkout:${accountId}`);
      sessionStorage.setItem(
        `studio-checkout-balance:${accountId}`,
        String(wallet?.available ?? 0),
      );
      const result = await request<{ url: string }>('POST', `${ROOT}/checkout`, { body: { pack } });
      const url = new URL(result.url);
      if (url.protocol !== 'https:' || url.hostname !== 'checkout.stripe.com')
        throw new Error('Invalid checkout destination.');
      window.location.assign(url.href);
    });
  }
  async function loadEarlier() {
    if (!thread || !id) return;
    const query = new URLSearchParams();
    if (thread.history?.messagesBefore) query.set('messagesBefore', thread.history.messagesBefore);
    if (thread.history?.requestsBefore) query.set('requestsBefore', thread.history.requestsBefore);
    const older = await request<Thread>('GET', `${ROOT}/threads/${id}?${query}`);
    setThread((current) => ({ ...mergeThread(current, older), history: older.history }));
  }
  const onVersionAction = (kind: 'host' | 'publish', v: Delivered) =>
    void action(async () => {
      if (kind === 'publish') {
        await api.updateMap(v.map_id, { visibility: 'public' });
        navigate(`/maps/${v.map_id}`);
      } else {
        const result = await request<{ code: string }>(
          'POST',
          `${ROOT}/threads/${id}/versions/${v.id}/room`,
          { body: {} },
        );
        window.location.assign(`/play/?join=${encodeURIComponent(result.code)}`);
      }
    });
  const landing = !id && wallet && !wallet.available && !wallet.activeRequest;
  return (
    <div
      className={`map-studio ms-root ${landing ? 'ms-landing-root' : 'ms-workspace-root'}`}
      data-hidden={hidden}
    >
      <header className="ms-header">
        <div className="ms-brand">
          <span className="ms-emblem" aria-hidden="true">
            ✧
          </span>
          <div>
            <span className="ms-eyebrow">GLOBULATION 2</span>
            <h1>AI Map Studio</h1>
          </div>
        </div>
        <details className="ms-projects">
          <summary>
            {thread?.title ?? 'Your projects'} <span aria-hidden="true">⌄</span>
          </summary>
          <nav aria-label="Map projects">
            <Link to="/map-studio">＋ New map</Link>
            {threads.map((t) => (
              <Link
                key={t.id}
                to={`/map-studio/${t.id}`}
                aria-current={id === t.id ? 'page' : undefined}
              >
                {t.title}
              </Link>
            ))}
            {!threads.length && <p>Your projects will be saved here.</p>}
          </nav>
        </details>
        <button
          className="ms-credit-button"
          onClick={() => setCredits(!credits)}
          aria-expanded={credits}
        >
          <span aria-hidden="true">✦</span> {wallet?.available ?? '…'} <span>credits</span>
        </button>
      </header>
      {payment === 'returned' && (
        <div className="ms-banner" role="status">
          {wallet && wallet.available > checkoutBalance
            ? 'Your credits are ready. Let’s create.'
            : 'Confirming your payment. Your credits appear once payment is confirmed.'}
        </div>
      )}
      {payment === 'cancelled' && (
        <div className="ms-banner" role="status">
          Checkout was cancelled. Your project and draft are saved.
        </div>
      )}
      {connection && (
        <div className="ms-banner" role="status">
          {connection}
        </div>
      )}
      {error && (
        <div className="ms-banner ms-error" role="alert">
          {error}
        </div>
      )}
      {pending && (
        <div className="ms-banner">
          A saved request is ready to send again safely.{' '}
          <button disabled={busy} onClick={() => void action(() => submitPending(pending))}>
            Retry the same request
          </button>
        </div>
      )}
      {credits && (
        <CreditControls wallet={wallet} busy={busy} buy={buy} close={() => setCredits(false)} />
      )}
      {wallet && !wallet.enabled && (
        <div className="ms-banner">
          AI Map Studio is not enabled on this instance. Your saved projects remain available.
        </div>
      )}
      {!id && !wallet ? (
        <div className="ms-gate" role="status">
          <p>Opening your studio…</p>
          {error && <button onClick={refreshWallet}>Retry loading studio</button>}
        </div>
      ) : landing ? (
        <MapStudioLanding
          wallet={wallet}
          busy={busy}
          buy={buy}
          threads={threads}
          draft={draft}
          setDraft={setDraft}
        />
      ) : (
        <StudioWorkspace
          id={id}
          thread={thread}
          wallet={wallet}
          busy={busy || !!pending}
          draft={draft}
          setDraft={setDraft}
          send={sendMessage}
          settings={settings}
          changeSettings={(next) => {
            setSettings(next);
            setParent(undefined);
          }}
          parent={parent}
          revise={(v) => {
            setParent(v?.id);
            if (v) setSettings(v.input.settings);
          }}
          revision={revision}
          celebrate={celebrate}
          loadEarlier={() => void action(loadEarlier)}
          versionAction={onVersionAction}
        />
      )}
    </div>
  );
}
function errorMessage(error: unknown) {
  return error instanceof Error ? error.message : 'The request could not be completed.';
}
function isRejectedSubmission(error: unknown) {
  return error instanceof ApiError && [400, 401, 403, 404, 409, 422, 429].includes(error.status);
}
