import { artifactLabel } from '../../i18n.tsx';
import { statusLabel } from '../../i18n.tsx';
import { displayMessage } from '../../i18n.tsx';
import { t, tp, useLocale, RichMessage } from '../../i18n.tsx';
import { cancellationReason } from '../../components/studio/adapters.ts';
import { Icon } from '../../icons.tsx';
import {
  StudioWorkspace as SharedWorkspace,
  ChatComposer,
  ConversationPane,
  StudioTabs,
} from '../../components/studio/Studio.tsx';
import { useEffect, useRef, useState } from 'react';
import type { MusicRelease, MusicStudioSettings, MusicStudioProgress } from '@glob2/protocol';
import { request } from '../../api.ts';
import { MusicPlayer } from '../../music/Player.tsx';
import { Cover } from '../../music/Cover.tsx';
import type { PlaybackSettings, PlaybackSnapshot } from '../../music/useMusicPlayback.ts';
import { MusicValidation, MusicCheckDetails } from '../../music/Validation.tsx';
import { useDeliveryCelebration } from '../studio/useDeliveryCelebration.ts';
import { MUSIC_IDEAS } from './ideas.ts';
import { ROOT, type Thread, type Wallet, type Delivered } from './types.ts';
interface Props {
  id?: string;
  thread?: Thread;
  wallet?: Wallet;
  busy: boolean;
  draft: string;
  setDraft: (s: string) => void;
  send: () => void;
  settings: MusicStudioSettings;
  changeSettings: (s: MusicStudioSettings) => void;
  parent?: string;
  revise: (v: Delivered | undefined) => void;
  revision: number;
  celebrate?: string;
  loadEarlier: () => void;
  openCredits?: () => void;
  versionAction: (v: Delivered, license: string) => void;
}
export function MusicWorkspace(p: Props) {
  useLocale();
  const [view, setView] = useState('preview');
  const [follow, setFollow] = useState(true),
    [selected, setSelected] = useState('');
  const [focusChat, setFocusChat] = useState(0);
  const [progress, setProgress] = useState<MusicStudioProgress>(),
    [release, setRelease] = useState<MusicRelease>(),
    [error, setError] = useState('');
  const [compare, setCompare] = useState(''),
    [preview, setPreview] = useState<{ requestId: string; url: string }>();
  const [initialPosition, setInitialPosition] = useState(0);
  const [initialSettings, setInitialSettings] = useState<PlaybackSettings>({});
  const [comparisonChoice, setComparisonChoice] = useState('');
  const [alignmentNote, setAlignmentNote] = useState('');
  const playback = useRef<PlaybackSnapshot>({
    position: 0,
    mood: 0,
    volume: 0.8,
    muted: false,
    playing: false,
  });
  const [cancelling, setCancelling] = useState(false);
  const position = useRef(0),
    previousFrames = useRef(0),
    previousTimeline = useRef<string | undefined>(undefined);
  const active = p.thread?.requests.find((r) => !['ready', 'failed'].includes(r.status));
  const generations = p.thread?.requests.filter((r) => r.kind === 'generate') ?? [];
  const latestGeneration = generations.at(-1);
  const versions = generations.filter(
    (r): r is Delivered => r.status === 'ready' && !!r.release_id && !!r.input.settings,
  );
  const current = generations.find((r) => r.id === (follow ? generations.at(-1)?.id : selected));
  const displayed = compare
    ? versions.find((v) => v.id === compare)
    : versions.find((v) => v.id === current?.id);
  const celebration = useDeliveryCelebration(
    p.celebrate,
    current?.id,
    !!displayed,
    follow && !compare,
  );
  // The evidence follows the audible revision, including when comparing history.
  const inspected = displayed ?? current;
  const inspectedId = inspected?.id,
    releaseId = displayed?.release_id;
  const previewUrl = preview?.requestId === inspectedId ? preview?.url : undefined;
  useEffect(() => {
    if (!p.id || !inspectedId) return;
    const abort = new AbortController();
    void request<MusicStudioProgress>(
      'GET',
      `${ROOT}/threads/${p.id}/requests/${inspectedId}/progress`,
      { signal: abort.signal },
    )
      .then((value) => {
        if (!abort.signal.aborted) {
          setProgress(value);
          setError('');
        }
      })
      .catch((e) => {
        if (!abort.signal.aborted) setError(String(e));
      });
    return () => abort.abort();
  }, [p.id, inspectedId, p.revision]);
  useEffect(() => {
    if (!releaseId) return;
    const abort = new AbortController();
    void request<MusicRelease>('GET', `/api/v1/music/${releaseId}`, {
      signal: abort.signal,
    })
      .then((value) => {
        if (abort.signal.aborted) return;
        const aligned =
          value.frames === previousFrames.current &&
          !!value.timelineId &&
          value.timelineId === previousTimeline.current;
        setAlignmentNote(
          previousFrames.current && !aligned
            ? t(
                'This version has a different or unverified musical timeline. Playback starts from the beginning.',
              )
            : '',
        );
        if (
          value.frames !== previousFrames.current ||
          !value.timelineId ||
          value.timelineId !== previousTimeline.current
        )
          position.current = 0;
        previousTimeline.current = value.timelineId;
        previousFrames.current = value.frames;
        setInitialPosition(position.current);
        setInitialSettings({
          ...playback.current,
          position: position.current,
          playing: playback.current.playing && !document.hidden,
        });
        setPreview(undefined);
        setRelease(value);
      })
      .catch((e) => {
        if (!abort.signal.aborted) setError(String(e));
      });
    return () => abort.abort();
  }, [releaseId]);
  const shown = progress?.requestId === inspectedId ? progress : undefined;
  const playable = release?.id === displayed?.release_id ? release : undefined;
  const canSend = !!p.wallet?.enabled && !!p.wallet.available && !p.busy && !active;
  async function cancel() {
    if (!active || !p.id) return;
    setCancelling(true);
    try {
      await request('POST', `${ROOT}/threads/${p.id}/requests/${active.id}/cancel`, { body: {} });
    } catch (e) {
      setError(String(e));
    } finally {
      setCancelling(false);
    }
  }
  return (
    <SharedWorkspace
      focusChat={focusChat}
      result={
        latestGeneration &&
        (latestGeneration.status === 'ready' || latestGeneration.status === 'failed')
          ? {
              id: `${latestGeneration.id}:${latestGeneration.status}`,
              status: latestGeneration.status,
              text:
                latestGeneration.status === 'ready'
                  ? 'Music creation ready in Preview.'
                  : 'Music creation needs attention in Preview.',
            }
          : undefined
      }
      attention={
        active ? 'Working' : inspected?.status === 'failed' ? 'Needs attention' : undefined
      }
      conversation={
        <>
          <div className="mu-chat-heading">
            <span className="ms-eyebrow">{t('YOUR COMPOSER')}</span>
            <h2>{p.parent ? t('Refine your soundtrack') : t('Start with a feeling')}</h2>
          </div>
          <ConversationPane
            label={t('Music composition conversation')}
            count={p.thread?.messages.length ?? 0}
            firstMessageId={p.thread?.messages[0]?.id}
          >
            {(p.thread?.history?.messagesBefore || p.thread?.history?.requestsBefore) && (
              <button disabled={p.busy} onClick={p.loadEarlier}>
                {t('Load earlier conversation and versions')}
              </button>
            )}
            {!p.thread?.messages.length && (
              <>
                <p>
                  {t(
                    'Tell me about the world you want to hear. We’ll shape the melody, instruments and three moods together.',
                  )}
                </p>
                {MUSIC_IDEAS.map(([title, text]) => (
                  <button key={title} className="mu-prompt" onClick={() => p.setDraft(text)}>
                    <RichMessage source={'{slot0} ↗'} slots={{ slot0: title }} />
                  </button>
                ))}
              </>
            )}
            {p.thread?.messages.map((m) => (
              <article key={m.id} className={`mu-message ${m.role}`}>
                <small>{m.role === 'user' ? t('YOU') : t('COMPOSER')}</small>
                <p>{m.text}</p>
              </article>
            ))}
            {active && (
              <p className="mu-working" role="status">
                {active.status === 'uncertain'
                  ? t('The provider outcome needs reconciliation. Your credit remains reserved.')
                  : t('Your composer is working…')}
              </p>
            )}
          </ConversationPane>
          <ChatComposer
            value={p.draft}
            onChange={p.setDraft}
            onSend={p.send}
            onBlocked={
              !p.busy && !active && p.wallet?.enabled && !p.wallet.available
                ? p.openCredits
                : undefined
            }
            label={t('Your idea or next change')}
            placeholder={t('Keep the melody, but make calm more spacious…')}
            target={
              p.parent
                ? t('Editing version {value0}', {
                    value0: generations.findIndex((v) => v.id === p.parent) + 1,
                  })
                : 'New soundtrack'
            }
            disabledReason={
              !canSend
                ? active || p.busy
                  ? 'Wait for the current request to finish.'
                  : !p.wallet?.enabled
                    ? 'Generation is unavailable. Saved music remains accessible.'
                    : 'An available Music credit is needed to chat or build.'
                : undefined
            }
            pricing="Discussion spends no credits; 1 available Music credit is required. Creation requests build automatically · 1 credit on delivery. Includes all three moods."
            tools={
              <>
                {p.parent && (
                  <button type="button" onClick={() => p.revise(undefined)}>
                    {t('Start fresh')}
                  </button>
                )}
                <div className="mu-settings">
                  <label>
                    {t('Sound palette')}
                    <select
                      value={p.settings.pipeline}
                      disabled={!!active || !!p.parent}
                      onChange={(e) =>
                        p.changeSettings({
                          ...p.settings,
                          pipeline: e.target.value as MusicStudioSettings['pipeline'],
                        })
                      }
                    >
                      <option value="acoustic-v1">
                        {t('Acoustic · woodwinds, strings & folk')}
                      </option>
                      <option value="synth-v1">{t('Synth · warm bells, pads & pulse')}</option>
                    </select>
                  </label>
                  <label>
                    {t('Seed')}
                    <input
                      type="number"
                      min={0}
                      max={2147483647}
                      value={p.settings.seed}
                      disabled={!!active}
                      onChange={(e) =>
                        p.changeSettings({
                          ...p.settings,
                          seed: Math.max(0, Math.min(2147483647, Math.floor(+e.target.value))),
                        })
                      }
                    />
                  </label>
                </div>
                <label>
                  {t('License for this version')}
                  <select
                    value={p.settings.license ?? 'CC-BY-4.0'}
                    disabled={!!active}
                    onChange={(e) =>
                      p.changeSettings({
                        ...p.settings,
                        license: e.target.value as MusicStudioSettings['license'],
                      })
                    }
                  >
                    <option value="CC-BY-4.0">{t('CC BY 4.0')}</option>
                    <option value="CC0-1.0">{t('CC0 1.0')}</option>
                    <option value="CC-BY-SA-4.0">{t('CC BY-SA 4.0')}</option>
                  </select>
                </label>
              </>
            }
          />
        </>
      }
      artifact={
        <>
          <StudioTabs
            label={t('Artifact view')}
            panels={{
              preview: 'studio-panel-artifact-view-preview',
              edit: 'studio-panel-artifact-view-edit',
              history: 'studio-panel-artifact-view-history',
            }}
            value={view}
            onChange={setView}
            items={[
              { id: 'preview', label: 'Preview', icon: 'eye' },
              { id: 'edit', label: 'Edit', icon: 'pencil' },
              { id: 'history', label: 'History', icon: 'restore' },
            ]}
          />
          <div
            id="studio-panel-artifact-view-edit"
            role="tabpanel"
            aria-labelledby="studio-tab-artifact-view-edit"
            hidden={view !== 'edit'}
          >
            <h2>{t('Edit your soundtrack')}</h2>
            <p>
              {t(
                'Keep listening and comparison controls in Preview. Select a delivered version to change through chat.',
              )}
            </p>
            <button
              aria-disabled={!displayed}
              onClick={() => {
                if (displayed) {
                  p.revise(displayed);
                  setFocusChat((n) => n + 1);
                }
              }}
            >
              {t('Edit this version in chat')}
            </button>
          </div>
          <div
            id="studio-panel-artifact-view-history"
            role="tabpanel"
            aria-labelledby="studio-tab-artifact-view-history"
            hidden={view !== 'history'}
          >
            <h2>{t('Version history')}</h2>
            {generations.map((v, i) => (
              <button
                key={v.id}
                onClick={() => {
                  setFollow(false);
                  setSelected(v.id);
                  setCompare('');
                  setView('preview');
                }}
              >
                <RichMessage
                  source={'Inspect version {slot0} · {slot1}'}
                  slots={{ slot0: i + 1, slot1: statusLabel(v.status) }}
                />
              </button>
            ))}
          </div>

          <section
            id="studio-panel-artifact-view-preview"
            role="tabpanel"
            aria-labelledby="studio-tab-artifact-view-preview"
            hidden={view !== 'preview'}
            className={`mu-inspector ${celebration ? 'mu-reveal' : ''}`}
          >
            <header className="mu-inspector-heading">
              <div>
                <span className="ms-eyebrow">{t('THE LISTENING ROOM')}</span>
                <h2>
                  {inspected
                    ? t('Version {value0}{value1}', {
                        value0: generations.indexOf(inspected) + 1,
                        value1: compare ? ' · comparing' : '',
                      })
                    : t('Your music takes shape here')}
                </h2>
              </div>
              <button
                aria-pressed={follow && !compare}
                onClick={() => {
                  setFollow(true);
                  setCompare('');
                  setComparisonChoice('');
                  setPreview(undefined);
                }}
              >
                {follow && !compare
                  ? t('Following latest generation')
                  : t('Follow latest generation')}
              </button>
            </header>
            {!!versions.length && (
              <nav className="mu-versions" aria-label={t('Music revisions')}>
                {versions.map((v) => (
                  <button
                    key={v.id}
                    aria-pressed={current?.id === v.id}
                    onClick={() => {
                      setSelected(v.id);
                      setFollow(false);
                      setCompare('');
                      setComparisonChoice('');
                      setPreview(undefined);
                    }}
                  >
                    {t('V')}
                    {generations.indexOf(v) + 1} <small>{statusLabel(v.status)}</small>
                  </button>
                ))}
              </nav>
            )}
            {error && <p role="alert">{displayMessage(error)}</p>}
            {inspected?.error && <p role="alert">{inspected.error}</p>}
            {!current && (
              <div className="mu-empty">
                <Icon name="music" size={80} />
                <h3>{t('One melody. Three moods.')}</h3>
                <p>
                  {t('Follow the composition, hear each candidate, and see what the checks find.')}
                </p>
                <div className="mu-mood-labels">
                  <span>{t('Calm')}</span>
                  <span>{t('Building')}</span>
                  <span>{t('Combat')}</span>
                </div>
              </div>
            )}
            {shown && !playable && (
              <p className="mu-generation-status" role="status">
                <Icon name="loader-2" size={18} />{' '}
                {shown.stages.find((stage) => stage.status === 'running')?.label ??
                  (inspected?.status === 'failed'
                    ? t('Generation needs attention')
                    : t('Preparing your soundtrack…'))}
              </p>
            )}
            {displayed && !playable && !error && <p role="status">{t('Loading this version…')}</p>}
            {active?.kind === 'generate' && inspectedId === active.id && (
              <>
                <button
                  disabled={cancelling || ['dispatched', 'uncertain'].includes(active.status)}
                  onClick={() => void cancel()}
                >
                  {t('Cancel generation')}
                </button>
                {cancellationReason(active.status) && <p>{cancellationReason(active.status)}</p>}
              </>
            )}
            {playable && !previewUrl && (
              <>
                <div className="mu-delivery-identity">
                  <Cover release={playable} />
                  <div>
                    <span className="music-eyebrow">
                      <Icon name="check" size={18} /> {t(' COMPOSITION COMPLETE')}
                    </span>
                    <h3>{playable.metadata.title ?? t('Ready to listen')}</h3>
                    <p>
                      {playable.metadata.description ??
                        t('Your colony’s soundtrack, in three moods.')}
                    </p>
                  </div>
                </div>
                {versions.length > 1 && (
                  <div className="mu-comparison">
                    <label>
                      {t('Compare with')}
                      <select
                        aria-label={t('Compare revision')}
                        value={comparisonChoice}
                        onChange={(e) => {
                          setSelected(current?.id ?? '');
                          setFollow(false);
                          setComparisonChoice(e.target.value);
                          setCompare(e.target.value);
                          setPreview(undefined);
                        }}
                      >
                        <option value="">{t('Choose a version')}</option>
                        {versions
                          .filter((v) => v.id !== current?.id)
                          .map((v) => (
                            <option key={v.id} value={v.id}>
                              <RichMessage
                                source={'Version {slot0}'}
                                slots={{ slot0: generations.indexOf(v) + 1 }}
                              />
                            </option>
                          ))}
                      </select>
                    </label>
                    <div className="seg" aria-label={t('Audible comparison version')}>
                      <button
                        aria-pressed={!compare}
                        onClick={() => {
                          setCompare('');
                          setPreview(undefined);
                        }}
                      >
                        <RichMessage
                          source={'A · V{slot0}'}
                          slots={{ slot0: current ? generations.indexOf(current) + 1 : '—' }}
                        />
                      </button>
                      <button
                        aria-pressed={!!compare}
                        disabled={!comparisonChoice}
                        onClick={() => {
                          setCompare(comparisonChoice);
                          setPreview(undefined);
                        }}
                      >
                        <RichMessage
                          source={'B{slot0}'}
                          slots={{
                            slot0: comparisonChoice
                              ? ` · V${generations.findIndex((v) => v.id === comparisonChoice) + 1}`
                              : '',
                          }}
                        />
                      </button>
                    </div>
                  </div>
                )}
                {alignmentNote && (
                  <p className="mu-alignment-note" role="status">
                    {alignmentNote}
                  </p>
                )}
                <MusicPlayer
                  key={playable.id}
                  release={playable}
                  initialPosition={initialPosition}
                  initialSettings={initialSettings}
                  onSnapshot={(snapshot) => {
                    playback.current = snapshot;
                    position.current = snapshot.position;
                  }}
                  onPosition={(value) => {
                    position.current = value;
                  }}
                />
                <div className="mu-actions">
                  <a className="btn" href={`/api/v1/music/${playable.id}/download`}>
                    {t('Download set')}
                  </a>
                  <button
                    disabled={!!active}
                    onClick={() => {
                      if (displayed) p.revise(displayed);
                      setFocusChat((v) => v + 1);
                    }}
                  >
                    {t('Revise this version')}
                  </button>
                  <button
                    disabled={p.busy || playable.status === 'published'}
                    onClick={() => {
                      if (displayed) p.versionAction(displayed, playable.metadata.license);
                    }}
                  >
                    {playable.status === 'published'
                      ? t('Published')
                      : t('Publish to music library')}
                  </button>
                </div>
                <p className="mu-private">
                  <RichMessage
                    source={'{slot0} {slot1} · AI composition is disclosed.'}
                    slots={{
                      slot0:
                        playable.status === 'published'
                          ? t('This version is public.')
                          : t('Saved privately. Publishing shares only this version.'),
                      slot1: playable.metadata.license,
                    }}
                  />
                </p>
              </>
            )}
            {previewUrl && (
              <div className="mu-candidate">
                <p>{t('Candidate preview · may need repairs')}</p>
                <audio key={previewUrl} src={previewUrl} controls />
                <button onClick={() => setPreview(undefined)}>{t('Return to final set')}</button>
              </div>
            )}
            {shown?.checks.length || playable?.validation || playable?.warnings?.length ? (
              <MusicValidation
                checks={playable?.validation ?? shown?.checks ?? []}
                warnings={playable?.warnings ?? []}
                latestAttempts
              />
            ) : null}
            {!!generations.length && (
              <details className="mu-history">
                <summary>
                  <RichMessage
                    source={'Generation history {slot0}'}
                    slots={{
                      slot0: (
                        <span>{tp('{count} version', '{count} versions', generations.length)}</span>
                      ),
                    }}
                  />
                </summary>
                <nav className="mu-history-versions" aria-label={t('All generation attempts')}>
                  {generations.map((v, i) => (
                    <button
                      key={v.id}
                      aria-pressed={inspectedId === v.id}
                      onClick={() => {
                        setSelected(v.id);
                        setFollow(false);
                        setCompare('');
                        setComparisonChoice('');
                        setPreview(undefined);
                      }}
                    >
                      <RichMessage
                        source={'V{slot0} · {slot1}'}
                        slots={{ slot0: i + 1, slot1: statusLabel(v.status) }}
                      />
                    </button>
                  ))}
                </nav>
                {shown && (
                  <>
                    <ol className="mu-stages" aria-label={t('Generation stages')}>
                      {shown.stages.map((stage) => (
                        <li key={stage.id} data-status={stage.status}>
                          <span aria-hidden="true">
                            {stage.status === 'complete' ? (
                              <Icon name="check" size={18} />
                            ) : stage.status === 'running' ? (
                              '◉'
                            ) : stage.status === 'failed' ? (
                              '×'
                            ) : (
                              '○'
                            )}
                          </span>
                          <div>
                            <strong>{t(stage.label)}</strong>
                            <small>{statusLabel(stage.detail ?? stage.status)}</small>
                          </div>
                        </li>
                      ))}
                    </ol>
                    {!!shown.artifacts.length && (
                      <details className="mu-artifacts">
                        <summary>{t('Candidate previews & reports')}</summary>
                        {shown.artifacts.map((a) =>
                          a.kind === 'preview' ? (
                            <button
                              key={a.id}
                              onClick={() => setPreview({ requestId: shown.requestId, url: a.url })}
                            >
                              {artifactLabel(a)}
                            </button>
                          ) : (
                            <a key={a.id} href={a.url} target="_blank" rel="noreferrer">
                              {artifactLabel(a)}
                            </a>
                          ),
                        )}
                      </details>
                    )}
                    {!!shown.notes.length && (
                      <details className="mu-notes">
                        <summary>{t('Composer’s progress')}</summary>
                        {shown.notes.map((n, i) => (
                          <p key={i}>
                            <small>
                              <RichMessage
                                source={'Candidate {slot0}'}
                                slots={{ slot0: n.attempt }}
                              />
                            </small>{' '}
                            {n.text}
                          </p>
                        ))}
                      </details>
                    )}
                    {!!shown.checks.length && (
                      <details className="mu-attempt-checks">
                        <summary>{t('All candidate measurements')}</summary>
                        {shown.checks.map((c) => (
                          <MusicCheckDetails key={c.id} check={c} showAttempt />
                        ))}
                      </details>
                    )}
                  </>
                )}
              </details>
            )}
          </section>
        </>
      }
    />
  );
}
