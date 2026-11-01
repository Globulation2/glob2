import { MessageError } from '../i18n.tsx';
import { displayMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useVersionApplication } from '../components/studio/useVersionApplication.ts';
import { studioSession } from '../components/studio/storage.ts';
import { StudioShell, StudioHeader, ReleaseDialog } from '../components/studio/Studio.tsx';
import { Icon } from '../icons.tsx';
import { useCallback, useEffect, useState } from 'react';
import { ApiError, request } from '../api.ts';
import { Link, useRouter } from '../router.tsx';
import { useSession } from '../state.tsx';
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
  useLocale();
  const { account } = useSession();
  if (account === undefined)
    return (
      <div className="ms-gate" role="status">
        {t('Opening your studio…')}
      </div>
    );
  if (account?.kind !== 'registered')
    return (
      <div className="ms-gate">
        <span className="ms-eyebrow">{t('AI MUSIC STUDIO')}</span>
        <h1>{t('Your colony has a sound.')}</h1>
        <p>
          {t(
            'Sign in with a registered account to compose, refine, and share your own soundtrack.',
          )}
        </p>
        <a className="btn primary" href="/signin">
          {t('Sign in to create')}
        </a>
      </div>
    );
  return <RegisteredStudio key={`${account.id}:${id ?? 'new'}`} id={id} accountId={account.id} />;
}
function RegisteredStudio({ id, accountId }: { id?: string; accountId: string }) {
  useLocale();
  const { navigate } = useRouter();
  const [wallet, setWallet] = useState<Wallet>();
  const [threads, setThreads] = useState<{ id: string; title: string }[]>([]);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [credits, setCredits] = useState(false);
  const [, setHidden] = useState(document.hidden);
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
    studioSession.getItem(`music-studio-checkout-balance:${accountId}`) ?? 0,
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
    const returnProject = studioSession.getItem(`music-studio-checkout:${accountId}`);
    if (new URLSearchParams(window.location.search).has('payment') && returnProject) {
      studioSession.removeItem(`music-studio-checkout:${accountId}`);
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
      const createdKey = `music-studio-created:${accountId}`;
      type Creation = {
        id: string;
        title: string;
        messageId: string;
        text: string;
        settings: typeof settings;
      };
      const stored = studioSession.getItem(createdKey);
      const creation: Creation = stored
        ? (JSON.parse(stored) as Creation)
        : {
            id: crypto.randomUUID(),
            title: draft.trim().slice(0, 128),
            messageId: crypto.randomUUID(),
            text: draft.trim(),
            settings,
          };
      studioSession.setItem(createdKey, JSON.stringify(creation));
      try {
        await request<{ id: string }>('POST', `${ROOT}/threads`, {
          body: { id: creation.id, title: creation.title },
        });
      } catch (error) {
        if (isRejectedSubmission(error)) studioSession.removeItem(createdKey);
        throw error;
      }
      const created = creation.id;
      const pending: Pending = {
        path: `${ROOT}/threads/${created}/turns`,
        body: { id: creation.messageId, text: creation.text, settings: creation.settings },
      };
      studioSession.setItem(`music-studio-draft:${accountId}:${created}`, creation.text);
      studioSession.setItem(
        `music-studio-pending:${accountId}:${created}`,
        JSON.stringify(pending),
      );
      studioSession.setItem(`music-studio-autosend:${accountId}:${created}`, '1');
      studioSession.setItem(
        `music-studio-settings:${accountId}:${created}`,
        JSON.stringify({ settings: creation.settings }),
      );
      if (studioSession.getItem(key)?.trim() === creation.text) studioSession.removeItem(key);
      studioSession.removeItem(createdKey);
      navigate(`/music-studio/${created}`);
    });
  }
  // Only the prompt-first navigation marks a request for automatic submission. Reloaded failures remain explicit retries.
  useEffect(() => {
    if (!id || !pending || !studioSession.getItem(`music-studio-autosend:${accountId}:${id}`))
      return;
    studioSession.removeItem(`music-studio-autosend:${accountId}:${id}`);
    queueMicrotask(() => {
      void action(() => submitPending(pending));
    });
    // Submission belongs to this mounted project, not subsequent state changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  function buy(pack: string) {
    void action(async () => {
      if (id) studioSession.setItem(`music-studio-checkout:${accountId}`, id);
      else studioSession.removeItem(`music-studio-checkout:${accountId}`);
      studioSession.setItem(
        `music-studio-checkout-balance:${accountId}`,
        String(wallet?.available ?? 0),
      );
      const result = await request<{ url: string }>('POST', `${ROOT}/checkout`, { body: { pack } });
      const url = new URL(result.url);
      if (url.protocol !== 'https:' || url.hostname !== 'checkout.stripe.com')
        throw new MessageError('Invalid checkout destination.');
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
  const [release, setRelease] = useState<Delivered>();
  const versionUndo = useVersionApplication(
    `music-version-undo:${accountId}:${id}`,
    thread?.requests,
    parent,
    (v) => {
      setParent(v?.id);
      if (v?.input.settings) setSettings(v.input.settings);
    },
    setParent,
  );
  return (
    <StudioShell className="music-studio ms-root ms-workspace-root">
      <StudioHeader title={t('AI Music Studio')} icon="music">
        <details className="ms-projects">
          <summary>
            {thread?.title ?? t('Your projects')} <Icon name="chevron-down" size={18} />
          </summary>
          <nav aria-label={t('Music projects')}>
            <Link to="/music-studio">{t('New composition')}</Link>
            {threads.map((t) => (
              <Link
                key={t.id}
                to={`/music-studio/${t.id}`}
                aria-current={id === t.id ? 'page' : undefined}
              >
                {t.title}
              </Link>
            ))}
            {!threads.length && <p>{t('Your projects will be saved here.')}</p>}
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
                {t('Delete conversation history')}
              </button>
            )}
          </nav>
        </details>
        <button
          className="ms-credit-button"
          aria-label={t('{value0} Music credits', { value0: wallet?.available ?? '…' })}
          onClick={() => setCredits(!credits)}
          aria-expanded={credits}
        >
          <Icon name="coins" size={18} /> {wallet?.available ?? '…'}{' '}
          <span>{t('Music credits')}</span>
        </button>
      </StudioHeader>
      {payment === 'returned' && (
        <div className="ms-banner" role="status">
          {wallet && wallet.available > checkoutBalance
            ? t('Your credits are ready. Let’s create.')
            : t('Confirming your payment. Your credits appear once payment is confirmed.')}
        </div>
      )}
      {payment === 'cancelled' && (
        <div className="ms-banner" role="status">
          {t('Checkout was cancelled. Your project and draft are saved.')}
        </div>
      )}
      {connection && (
        <div className="ms-banner" role="status">
          {connection}
        </div>
      )}
      {error && (
        <div className="ms-banner ms-error" role="alert">
          {displayMessage(error)}
        </div>
      )}
      {pending && (
        <div className="ms-banner">
          {t('A saved request is ready to send again safely.')}{' '}
          <button disabled={busy} onClick={() => void action(() => submitPending(pending))}>
            {t('Retry the same request')}
          </button>
        </div>
      )}
      {versionUndo.version && (
        <div className="studio-revisions" role="status">
          <span>{t('Generated version accepted as your current edit target.')}</span>
          <button aria-disabled={!versionUndo.canUndo} onClick={versionUndo.undo}>
            <Icon name="restore" size={18} /> {t(' Undo')}
          </button>
          {!versionUndo.canUndo && (
            <span>{t('The edit target changed. Choose a saved version in history.')}</span>
          )}
        </div>
      )}
      <ReleaseDialog open={credits} onClose={() => setCredits(false)} title={t('Music credits')}>
        <CreditControls wallet={wallet} busy={busy} buy={buy} close={() => setCredits(false)} />
      </ReleaseDialog>
      {wallet && !wallet.enabled && (
        <div className="ms-banner">
          {t(
            'AI Music Studio is not enabled on this instance. Your saved projects remain available.',
          )}
        </div>
      )}
      {!id && !wallet ? (
        <div className="ms-gate" role="status">
          <p>{t('Opening your studio…')}</p>
          {error && <button onClick={refreshWallet}>{t('Retry loading studio')}</button>}
        </div>
      ) : (
        <MusicWorkspace
          id={id}
          openCredits={() => setCredits(true)}
          thread={thread}
          wallet={wallet}
          busy={busy || !!pending}
          draft={draft}
          setDraft={setDraft}
          send={sendMessage}
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
          versionAction={(version) => setRelease(version)}
        />
      )}
      <ReleaseDialog
        open={!!release}
        onClose={() => setRelease(undefined)}
        title={t('Publish music version')}
      >
        {release && (
          <>
            <p>
              <RichMessage
                source={
                  'Publish saved soundtrack {slot0} to the public Music library. Only this exact version is shared.'
                }
                slots={{ slot0: release.release_id?.slice(0, 12) }}
              />
            </p>
            <p>
              <RichMessage
                source={'License: {slot0} · AI composition is disclosed.'}
                slots={{ slot0: release.input.settings.license ?? 'CC-BY-4.0' }}
              />
            </p>
            <button
              className="primary"
              disabled={busy}
              onClick={() => {
                onVersionAction(release, release.input.settings.license ?? 'CC-BY-4.0');
                setRelease(undefined);
              }}
            >
              <Icon name="share" size={18} /> {t(' Publish')}
            </button>
          </>
        )}
      </ReleaseDialog>
    </StudioShell>
  );
}
function errorMessage(error: unknown) {
  return error instanceof Error ? error.message : t('The request could not be completed.');
}
function isRejectedSubmission(error: unknown) {
  return error instanceof ApiError && [400, 401, 403, 404, 409, 422, 429].includes(error.status);
}
