import { fixedCaption } from '../i18n.tsx';
import { statusLabel } from '../i18n.tsx';
import { message as sourceMessage, displayMessage } from '../i18n.tsx';
import { getLocale } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { cancellationReason } from '../components/studio/adapters.ts';
import { useRevisionUndo } from '../components/studio/useRevisionUndo.ts';
import { BuildingEditor } from './BuildingStudio.tsx';
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
  useLocale();
  const { account } = useSession();
  // Account changes must discard private snapshots and unsent request state,
  // even when the router stays on the same project URL.
  return <BuildingWorkspace key={`${account?.id ?? 'anonymous'}:${id ?? 'projects'}`} id={id} />;
}

function BuildingWorkspace({ id }: { id?: string }) {
  useLocale();
  const { account } = useSession(),
    { navigate, location } = useRouter();
  const [text, setText] = useStudioValue(
      `ai-building-studio-prompt:${account?.id}:${id ?? 'new'}`,
      '',
    ),
    [title, setTitle] = useState(t('New building family')),
    [error, setError] = useState(''),
    [notice, setNotice] = useState(''),
    [busy, setBusy] = useState(false),
    [retrying, setRetrying] = useState(false),
    [confirmDelete, setConfirmDelete] = useState(false);
  const [selectedRefs, setSelectedRefs] = useStudioValue<string[]>(
    `ai-building-studio-references:${account?.id}:${id ?? 'new'}`,
    [],
  );
  const [manualDirty, setManualDirty] = useState(false);
  const [editorRevision, setEditorRevision] = useState<string>();
  const [view, setView] = useState('preview');
  const [creditsOpen, setCreditsOpen] = useState(false);
  const [focusChat, setFocusChat] = useState(0);
  const [selected, setSelected] = useStudioValue(`building-view:${account?.id}:${id}`, ''),
    [compare, setCompare] = useState('');
  const [creationId] = useStudioValue(`building-studio-create:${account?.id}`, crypto.randomUUID());
  const submission = useRef<BuildingAiStudioTurn | null>(null),
    creation = useRef(creationId);
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
    [account?.id, id, setText],
  );
  const { wallet, projects, thread, draft, progress, loading, loadError, connection, refresh } =
    useStudioSnapshot(account?.id, id, reconcileSubmission);
  const pending = thread?.requests.findLast((r) => !['ready', 'failed'].includes(r.status));
  const latestGeneration = thread?.requests.filter((r) => r.kind === 'generate').at(-1);
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
  }, [account, id, setSelectedRefs, setText]);
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
      setError(e instanceof Error ? e.message : t('Request failed.'));
    } finally {
      setBusy(false);
    }
  }
  useEffect(() => {
    if (!manualDirty && draft) queueMicrotask(() => setEditorRevision(draft.revision));
  }, [draft, manualDirty]);
  const undo = useRevisionUndo(
    `building-undo:${account?.id}:${id}`,
    draft,
    thread?.revisions.filter((r) => r.applied && r.appliedRevision === draft?.revision) ?? [],
  );
  async function create() {
    const result = await request<{ id: string }>('POST', ROOT + '/threads', {
      body: {
        id: creation.current,
        title,
        ...(location.search.get('draft') ? { draftId: location.search.get('draft') } : {}),
      },
    });
    studioSession.removeItem(`building-studio-create:${account?.id}`);
    if (text.trim() && account) {
      const thread = await request<{ draftId: string }>('GET', `${ROOT}/threads/${result.id}`);
      const initial = await request<{ revision: string }>(
        'GET',
        '/api/v1/building-drafts/' + thread.draftId,
      );
      const turn = {
        id: crypto.randomUUID(),
        text,
        references: selectedRefs,
        expectedRevision: initial.revision,
      };
      studioSession.setItem(
        `ai-building-studio-turn:${account.id}:${result.id}`,
        JSON.stringify(turn),
      );
      try {
        await request('POST', `${ROOT}/threads/${result.id}/turns`, { body: turn });
        studioSession.removeItem(`ai-building-studio-turn:${account.id}:${result.id}`);
      } finally {
        setText('');
        navigate('/ai-building-studio/' + result.id);
      }
    } else navigate('/ai-building-studio/' + result.id);
  }
  async function send() {
    if (!draft || !account || !id || pending || manualDirty || !wallet?.enabled) return;
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
    rememberTurn(`ai-building-studio-turn:${account.id}:${id}`, value);
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
    if (
      manualDirty ||
      !window.confirm(
        t('Restore this version as the current draft? The current version stays in history.'),
      )
    )
      return;
    await request('POST', `${ROOT}/threads/${id}/requests/${requestId}/adopt`, {
      body: { expectedRevision: draft.revision },
    });
    setSelected('');
    setFocusChat((v) => v + 1);
    setNotice(sourceMessage('Revision restored to your saved draft.'));
  }
  const selectedRevision = thread?.revisions.find((r) => r.requestId === selected);
  const compared =
    compare !== selected ? thread?.revisions.find((r) => r.requestId === compare) : undefined;
  const currentLabel = selectedRevision?.title ?? 'Current saved draft';
  if (!account)
    return (
      <section>
        <h1>{t('AI Building Studio')}</h1>
        <p>{t('Describe a building. Create its artwork and the way it works.')}</p>
        <a href="/signin">{t('Sign in to create a building')}</a>
      </section>
    );
  return (
    <StudioShell className="ai-building-studio">
      <StudioHeader title={thread?.title ?? t('AI Building Studio')} icon="building">
        <Link to="/ai-building-studio">
          <Icon name="folder-open" size={18} /> {t(' Projects')}
        </Link>
        <button onClick={() => setCreditsOpen(true)}>
          <Icon name="coins" size={18} /> {wallet?.available ?? '…'} {t(' Building credits')}
        </button>
      </StudioHeader>
      {(error || loadError) && <p role="alert">{error || loadError}</p>}
      {notice && <p role="status">{displayMessage(notice)}</p>}
      {loading && <p role="status">{t('Loading your building workspace…')}</p>}
      {connection && <p role="status">{connection}</p>}
      {wallet && !wallet.enabled && (
        <p role="status">
          {t('Building Studio is disabled on this instance. Your saved projects remain available.')}
        </p>
      )}
      <ReleaseDialog
        open={creditsOpen}
        onClose={() => setCreditsOpen(false)}
        title={t(' Building credits')}
      >
        <CreditPanel domain="Building" available={wallet?.available} reserved={wallet?.reserved}>
          {' '}
          <p>
            {t(
              'Questions and brainstorming are free. A creation or revision request starts one build of up to 12 building variants. Failed generations return their reservation.',
            )}
          </p>
          {wallet && wallet.packs.length === 0 && (
            <p role="status">{t('Credit purchases are currently unavailable.')}</p>
          )}
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
              <RichMessage
                source={'Buy {slot0} credits · {slot1}'}
                slots={{
                  slot0: p.credits,
                  slot1: new Intl.NumberFormat(getLocale(), {
                    style: 'currency',
                    currency: p.currency,
                  }).format(p.amount / 100),
                }}
                singular={'Buy {slot0} credit · {slot1}'}
                count={Number(p.credits)}
              />
            </button>
          ))}
        </CreditPanel>
      </ReleaseDialog>
      {!id ? (
        <NewStudio
          title={t('Your building')}
          value={text}
          onChange={setText}
          onSend={() => void action(create)}
          disabledReason={
            busy
              ? 'Saving your project…'
              : !wallet?.enabled
                ? 'Generation is unavailable.'
                : !wallet.available
                  ? 'An available Building credit is needed to chat or build.'
                  : undefined
          }
          onBlocked={
            !busy && wallet?.enabled && !wallet.available ? () => setCreditsOpen(true) : undefined
          }
          pricing="Discussion spends no credits; 1 available Building credit is required. Creation requests build automatically · 1 credit on delivery."
          projects={
            <nav aria-label={t('Building projects')}>
              {projects.map((p) => (
                <Link key={p.id} to={'/ai-building-studio/' + p.id}>
                  {p.title}
                </Link>
              ))}
            </nav>
          }
          tools={
            <label>
              {t('Project title')}{' '}
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
                      ? 'Building creation ready in Preview.'
                      : 'Building creation needs attention in Preview.',
                }
              : undefined
          }
          conversation={
            <aside className="bas-chat">
              <h2>{t('Design conversation')}</h2>
              <details className="building-ai-project-options">
                <summary>{t('Project options')}</summary>
                <p>{t('Your saved building remains available in Building Studio.')}</p>
                {confirmDelete ? (
                  <div role="group" aria-label={t('Confirm history deletion')}>
                    <p>
                      {t(
                        'Delete this project’s conversation, references and generated revisions permanently?',
                      )}
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
                      {t('Delete history permanently')}
                    </button>
                    <button disabled={busy} onClick={() => setConfirmDelete(false)}>
                      {t('Keep history')}
                    </button>
                  </div>
                ) : (
                  <button disabled={busy || !!pending} onClick={() => setConfirmDelete(true)}>
                    {t('Delete project history')}
                  </button>
                )}
              </details>
              <ConversationPane
                label={t('Building design conversation')}
                count={thread?.messages.length ?? 0}
                firstMessageId={thread?.messages[0]?.id}
              >
                {thread?.messages.map((m) => (
                  <article key={m.id} className={'bas-message ' + m.role}>
                    <strong>{m.role === 'user' ? t('You') : t('Building designer')}</strong>
                    <p>{m.text}</p>
                  </article>
                ))}
                {thread && !thread.messages.length && (
                  <p className="building-ai-empty">
                    {t(
                      'Describe what your building should do and how it should look. You can also brainstorm before creating it.',
                    )}
                  </p>
                )}
              </ConversationPane>
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  void action(send);
                }}
              >
                <label>
                  {t('Describe your creation or ask a question')}
                  <p className="studio-edit-target">
                    <RichMessage
                      source={'Editing current saved draft · {slot0}'}
                      slots={{ slot0: draft?.revision }}
                    />
                  </p>
                  <ChatInput
                    rows={5}
                    maxLength={8000}
                    value={text}
                    disabled={retrying}
                    onChange={(e) => setText(e.target.value)}
                    placeholder={t(
                      'Create a mushroom hospital that heals workers and warriors, costs 10 wood, and holds four units.',
                    )}
                    onSend={() => {
                      if (
                        !busy &&
                        !pending &&
                        wallet?.enabled &&
                        text.trim() &&
                        draft &&
                        !manualDirty
                      )
                        void action(send);
                    }}
                  />
                </label>
                <p className="building-ai-cost">
                  {t(
                    'Discussion spends no credits; 1 available Building credit is required. Creation and edit requests start automatically and cost 1 credit on delivery.',
                  )}
                </p>
                <details className="studio-attachments">
                  <summary>
                    <Icon name="upload" size={18} /> {t(' Reference images')}
                  </summary>{' '}
                  <label>
                    {t(' Reference images')}
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
                    {t(
                      'Upload artwork you may use as a reference. Up to four selected images guide the visual style.',
                    )}
                  </small>
                  <div className="bas-references">
                    {thread?.references.map((r) => (
                      <label key={r.hash}>
                        <img src={r.url} alt={t(r.label)} />
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
                        {t('Use reference')}
                      </label>
                    ))}
                  </div>
                </details>{' '}
                <button
                  className="primary"
                  disabled={
                    busy || !!pending || !wallet?.enabled || !text.trim() || !draft || manualDirty
                  }
                >
                  <Icon name={retrying ? 'refresh' : 'send'} size={18} />{' '}
                  {retrying ? t('Retry saved request') : t('Send')}
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
                    {t('Discard local retry')}
                  </button>
                )}
                <p className="studio-compose-help">
                  {t(
                    'Enter to send · Shift + Enter for a new line. Creation requests build automatically · 1 credit on delivery.',
                  )}
                </p>
                {manualDirty && (
                  <p>{t('Save your manual edits before asking AI to revise the building.')}</p>
                )}
                {!wallet?.available && (
                  <p>{t('An available Building credit is needed to chat or build.')}</p>
                )}
              </form>
              {pending && (
                <div role="status">
                  <p>
                    {pending.status === 'uncertain'
                      ? t(
                          'This request is paused while its result is checked. Your reserved credit stays safe; you can leave and return later.',
                        )
                      : t('Working on your request…')}
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
                    {t('Cancel request')}
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
              {manualDirty && (
                <p role="status">
                  {t(
                    'Save your manual building edits in Edit before generating, restoring a version, or undoing.',
                  )}
                </p>
              )}
              {undo.delivered !== undefined && (
                <div className="studio-revisions" role="status">
                  <span>
                    <RichMessage
                      source={'Generated edits applied to revision {slot0}.'}
                      slots={{ slot0: undo.delivered }}
                    />
                  </span>
                  <button
                    aria-disabled={!undo.canUndo || manualDirty || !!pending}
                    onClick={() => {
                      if (!undo.canUndo || manualDirty || pending || !undo.before || !draft) return;
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
                    <Icon name="restore" size={18} /> {t(' Undo')}
                  </button>
                  {!undo.canUndo && (
                    <span>{t('A newer draft prevents undo. Use history to restore it.')}</span>
                  )}
                </div>
              )}
              <StudioTabs
                label={t('Artifact view')}
                panels={{
                  preview: 'studio-panel-artifact-view-preview',
                  edit: 'studio-panel-artifact-view-edit',
                  history: 'studio-panel-artifact-view-history',
                }}
                value={view}
                onChange={(next) => {
                  if (
                    manualDirty &&
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
                <div className="bas-preview">
                  <h2>{t('Your building')}</h2>
                  <RevisionControls
                    viewed={currentLabel}
                    target="current saved draft"
                    follow={selected ? () => setSelected('') : undefined}
                  />
                  {selectedRevision && (
                    <ValidationSummary version={selectedRevision.title}>
                      <p>
                        {selectedRevision.report.valid
                          ? t('Engine validation passed.')
                          : t('Engine validation failed.')}
                      </p>
                    </ValidationSummary>
                  )}
                  {progress &&
                    !selectedRevision &&
                    (pending ||
                      thread?.revisions.some(
                        (r) =>
                          r.requestId === progress.requestId &&
                          r.appliedRevision === draft?.revision,
                      )) && (
                      <>
                        <ol className="bas-stages">
                          {progress.stages.map((s) => (
                            <li key={s.id} data-status={s.status}>
                              <strong>{t(s.label)}</strong>
                              <span>{statusLabel(s.status)}</span>
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
                            <RichMessage
                              source={'{slot0}: {slot1} · {slot2}'}
                              slots={{
                                slot0: fixedCaption(c.label),
                                slot1: statusLabel(c.status),
                                slot2: c.detail,
                              }}
                            />
                          </p>
                        ))}
                      </>
                    )}
                  {!selectedRevision && thread?.requests.at(-1)?.error && (
                    <p role="alert">{thread.requests.at(-1)?.error}</p>
                  )}
                  {draft && (
                    <>
                      <label>
                        {t('View revision')}
                        <select value={selected} onChange={(e) => setSelected(e.target.value)}>
                          <option value="">{t('Current saved draft')}</option>
                          {thread?.revisions.map((r, i) => (
                            <option key={r.requestId} value={r.requestId}>
                              <RichMessage
                                source={'{slot0} · revision {slot1}'}
                                slots={{ slot0: r.title, slot1: thread.revisions.length - i }}
                              />
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
                          {t('Viewing a saved revision. Restore it to make it the current draft.')}
                        </p>
                      )}
                      <label>
                        {t('Compare with')}
                        <select
                          value={compare === selected ? '' : compare}
                          onChange={(e) => setCompare(e.target.value)}
                        >
                          <option value="">{t('No comparison')}</option>
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
                        <section aria-label={t('Comparison with ') + compared.title}>
                          <p className="building-ai-action-hint">
                            <RichMessage
                              source={'Compared revision: {slot0}'}
                              slots={{ slot0: compared.title }}
                            />
                          </p>
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
                              {t('Export viewed revision')}
                            </a>
                            <button
                              disabled={busy || !!pending}
                              aria-disabled={manualDirty}
                              onClick={() =>
                                void action(() => restoreRevision(selectedRevision.requestId))
                              }
                            >
                              {t('Edit this version')}
                            </button>
                          </>
                        )}
                      </div>
                      <button onClick={() => setView('edit')}>
                        <Icon name="pencil" size={18} /> {t(' Edit & release settings')}
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
                    <BuildingEditor
                      key={editorRevision}
                      initial={draft}
                      embedded
                      onDirtyChange={setManualDirty}
                      onSaved={() => void refresh()}
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
                <section aria-label={t('Saved draft history')}>
                  <h2>{t('Previous saved drafts')}</h2>
                  {thread?.draftHistory?.map((d) => (
                    <article key={d.revision}>
                      <h3>{d.title}</h3>
                      <p>{t('Saved draft · validation remains version-specific.')}</p>
                      <a href={`${ROOT}/threads/${id}/drafts/${d.revision}/file`}>
                        {t('Download saved draft')}
                      </a>
                      <button
                        disabled={busy || !!pending || manualDirty}
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
                            setSelected('');
                            setFocusChat((n) => n + 1);
                          })
                        }
                      >
                        {t('Edit this saved draft')}
                      </button>
                    </article>
                  ))}
                </section>{' '}
                {!!thread?.revisions.length && (
                  <details className="building-ai-history">
                    <summary>{t('Generated revisions')}</summary>
                    {thread.revisions.map((r) => (
                      <article key={r.requestId}>
                        <h3>{r.title}</h3>
                        <p>
                          <RichMessage
                            source={'{slot0} · {slot1}'}
                            slots={{
                              slot0: r.applied ? t('Delivered to draft') : t('Saved candidate'),
                              slot1: r.report.valid
                                ? t('Engine validation passed')
                                : t('Validation failed'),
                            }}
                          />
                        </p>
                        <a href={`${ROOT}/threads/${id}/revisions/${r.requestId}/file`}>
                          {t('Download candidate')}
                        </a>
                        <button
                          disabled={busy || !!pending || !draft}
                          aria-disabled={manualDirty}
                          onClick={() => void action(() => restoreRevision(r.requestId))}
                        >
                          {t('Restore this revision')}
                        </button>
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
