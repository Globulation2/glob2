import { useCallback, useEffect, useState } from 'react';
import { ApiError, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
import { MusicStudioLanding } from './MusicStudioLanding.tsx';
import { MusicWorkspace } from './music-studio/Workspace.tsx';
import { useMusicStudioStream } from './music-studio/useMusicStudioStream.ts';
import {
  clearMusicStudioDraft,
  useMusicStudioDraft,
  type Pending,
} from './music-studio/useMusicStudioDraft.ts';
import { CreditControls } from './music-studio/CreditControls.tsx';
import {
  ROOT,
  mergeThread,
  type Delivered,
  type Thread,
  type Wallet,
} from './music-studio/types.ts';
import '../styles/studio.css';
import './music-studio/music-studio.css';

export function MusicStudio({ id }: { id?: string }) {
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
        <span className="ms-eyebrow">AI MUSIC STUDIO</span>
        <h1>Your colony has a sound.</h1>
        <p>Sign in with a registered account to compose, refine, and share your own soundtrack.</p>
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
    setParent,
    draftKey: key,
  } = useMusicStudioDraft(accountId, id);
  const payment = new URLSearchParams(window.location.search).get('payment');
  const checkoutBalance = Number(
    sessionStorage.getItem(`music-studio-checkout-balance:${accountId}`) ?? 0,
  );
  const refreshWallet = useCallback(() => {
    void request<Wallet>('GET', `${ROOT}/account`)
      .then(setWallet)
      .catch((e) => setError(errorMessage(e)));
  }, []);
  const { thread, setThread, connection, revision, celebrate } = useMusicStudioStream(
    id,
    refreshWallet,
  );
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
    const returnProject = sessionStorage.getItem(`music-studio-checkout:${accountId}`);
    if (new URLSearchParams(window.location.search).has('payment') && returnProject) {
      sessionStorage.removeItem(`music-studio-checkout:${accountId}`);
      navigate(
        `/music-studio/${returnProject}?payment=${payment === 'cancelled' ? 'cancelled' : 'returned'}`,
        { replace: true },
      );
    } else if (wallet?.activeRequest)
      navigate(`/music-studio/${wallet.activeRequest.threadId}`, { replace: true });
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
          path: `${ROOT}/threads/${id}/messages`,
          body: { id: crypto.randomUUID(), text: draft.trim() },
        });
        return;
      }
      // Persist both identities before the first network call, including unknown outcomes.
      const createdKey = `music-studio-created:${accountId}`;
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
        path: `${ROOT}/threads/${created}/messages`,
        body: { id: creation.messageId, text: creation.text },
      };
      sessionStorage.setItem(`music-studio-draft:${accountId}:${created}`, creation.text);
      sessionStorage.setItem(
        `music-studio-pending:${accountId}:${created}`,
        JSON.stringify(pending),
      );
      sessionStorage.setItem(`music-studio-autosend:${accountId}:${created}`, '1');
      sessionStorage.setItem(
        `music-studio-settings:${accountId}:${created}`,
        JSON.stringify({ settings: creation.settings }),
      );
      if (sessionStorage.getItem(key)?.trim() === creation.text) sessionStorage.removeItem(key);
      sessionStorage.removeItem(createdKey);
      navigate(`/music-studio/${created}`);
    });
  }
  // Only the prompt-first navigation marks a request for automatic submission. Reloaded failures remain explicit retries.
  useEffect(() => {
    if (!id || !pending || !sessionStorage.getItem(`music-studio-autosend:${accountId}:${id}`))
      return;
    sessionStorage.removeItem(`music-studio-autosend:${accountId}:${id}`);
    queueMicrotask(() => {
      void action(() => submitPending(pending));
    });
    // Submission belongs to this mounted project, not subsequent state changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  function generate() {
    if (id)
      void action(() =>
        submitPending({
          path: `${ROOT}/threads/${id}/generate`,
          body: { id: crypto.randomUUID(), settings, ...(parent ? { parent } : {}) },
        }),
      );
  }
  function buy(pack: string) {
    void action(async () => {
      if (id) sessionStorage.setItem(`music-studio-checkout:${accountId}`, id);
      else sessionStorage.removeItem(`music-studio-checkout:${accountId}`);
      sessionStorage.setItem(
        `music-studio-checkout-balance:${accountId}`,
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
  const onVersionAction = (v: Delivered, license: string) =>
    void action(async () => {
      await request('POST', `/api/v1/music/${v.release_id}/publish`, { body: { license } });
      navigate(`/music/${v.release_id}`);
    });
  const landing = !id && wallet && !wallet.available && !wallet.activeRequest;
  return (
    <div
      className={`music-studio ms-root ${landing ? 'ms-landing-root' : 'ms-workspace-root'}`}
      data-hidden={hidden}
    >
      <header className="ms-header">
        <div className="ms-brand">
          <span className="ms-emblem" aria-hidden="true">
            ✧
          </span>
          <div>
            <span className="ms-eyebrow">GLOBULATION 2</span>
            <h1>AI Music Studio</h1>
          </div>
        </div>
        <details className="ms-projects">
          <summary>
            {thread?.title ?? 'Your projects'} <span aria-hidden="true">⌄</span>
          </summary>
          <nav aria-label="Music projects">
            <Link to="/music-studio">＋ New composition</Link>
            {threads.map((t) => (
              <Link
                key={t.id}
                to={`/music-studio/${t.id}`}
                aria-current={id === t.id ? 'page' : undefined}
              >
                {t.title}
              </Link>
            ))}
            {!threads.length && <p>Your projects will be saved here.</p>}
            {id && (
              <button
                disabled={busy}
                onClick={() => {
                  if (
                    !window.confirm(
                      'Delete this conversation and its source history? Finished music remains in your library.',
                    )
                  )
                    return;
                  void action(async () => {
                    await request('DELETE', `${ROOT}/threads/${id}`);
                    clearMusicStudioDraft(accountId, id);
                    navigate('/music-studio');
                  });
                }}
              >
                Delete conversation history
              </button>
            )}
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
          AI Music Studio is not enabled on this instance. Your saved projects remain available.
        </div>
      )}
      {!id && !wallet ? (
        <div className="ms-gate" role="status">
          <p>Opening your studio…</p>
          {error && <button onClick={refreshWallet}>Retry loading studio</button>}
        </div>
      ) : landing ? (
        <MusicStudioLanding
          wallet={wallet}
          busy={busy}
          buy={buy}
          threads={threads}
          draft={draft}
          setDraft={setDraft}
        />
      ) : (
        <MusicWorkspace
          id={id}
          thread={thread}
          wallet={wallet}
          busy={busy || !!pending}
          draft={draft}
          setDraft={setDraft}
          send={sendMessage}
          generate={generate}
          settings={settings}
          changeSettings={(next) => {
            setSettings(next);
            if (next.pipeline !== settings.pipeline) setParent(undefined);
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
