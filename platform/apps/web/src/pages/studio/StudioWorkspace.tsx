import { statusLabel } from '../../i18n.tsx';
import { t, useLocale, RichMessage, artifactLabel } from '../../i18n.tsx';
import { useRef, useState } from 'react';
import {
  StudioWorkspace as SharedWorkspace,
  ChatComposer,
  RequestStatus,
  StudioTabs,
} from '../../components/studio/Studio.tsx';
import type { StudioSettings } from '@glob2/protocol';
import { ART } from '../../art.tsx';
import { Link } from '../../router.tsx';
import { MapViewer } from './MapViewer.tsx';
import { Conversation } from './Conversation.tsx';
import { ValidationPanel } from './ValidationPanel.tsx';
import { GenerationTimeline } from './GenerationTimeline.tsx';
import { useStudioProgress } from './useStudioProgress.ts';
import { useDeliveryCelebration } from './useDeliveryCelebration.ts';
import {
  STAGES,
  preview,
  type Artifact,
  type Check,
  type Delivered,
  type StageId,
  type Thread,
  type Wallet,
} from './types.ts';
interface Props {
  id?: string;
  thread?: Thread;
  wallet?: Wallet;
  busy: boolean;
  draft: string;
  setDraft: (v: string) => void;
  send: () => void;
  settings: StudioSettings;
  changeSettings: (v: StudioSettings) => void;
  parent?: string;
  revise: (v: Delivered | undefined) => void;
  revision: number;
  celebrate?: string;
  loadEarlier: () => void;
  openCredits?: () => void;
  versionAction: (action: 'host' | 'publish', v: Delivered) => void;
}
export function StudioWorkspace(p: Props) {
  useLocale();
  const [view, setView] = useState('preview');
  const [focusChat, setFocusChat] = useState(0);
  const composerRef = useRef<HTMLTextAreaElement>(null);
  const focusComposer = () => {
    setFocusChat((v) => v + 1);
    requestAnimationFrame(() => composerRef.current?.focus());
  };
  const [follow, setFollow] = useState(true);
  const [inspected, setInspected] = useState<string>();
  const [stage, setStage] = useState<StageId>();
  const [artifactId, setArtifactId] = useState<string>();
  const [compare, setCompare] = useState('');
  const [check, setCheck] = useState<Check>();
  const [decodedUrl, setDecodedUrl] = useState<string>();
  const active = p.thread?.requests.find((r) => !['ready', 'failed'].includes(r.status));
  const generations = p.thread?.requests.filter((r) => r.kind === 'generate') ?? [];
  const latestGeneration = generations.at(-1);
  const versions = generations.filter(
    (r): r is Delivered => r.status === 'ready' && !!r.map_id && !!r.map_hash && !!r.input.settings,
  );
  const current = generations.find(
    (r) =>
      r.id ===
      (follow ? (active?.kind === 'generate' ? active.id : generations.at(-1)?.id) : inspected),
  );
  const delivered = versions.find((v) => v.id === current?.id);
  const comparison = versions.find((v) => v.id === compare);
  const currentId = current?.id;
  const { progress, progressError } = useStudioProgress(p.id, currentId, p.revision);
  const visibleProgress = progress?.requestId === current?.id ? progress : undefined;
  const stages = visibleProgress?.stages ?? STAGES;
  const selectedStage =
    stage ??
    [...stages]
      .reverse()
      .find((s) => s.status === 'complete' || s.status === 'running' || s.status === 'failed')
      ?.id ??
    'prepare';
  const stageArtifacts = visibleProgress?.artifacts.filter((a) => a.stage === selectedStage) ?? [];
  const artifact = stageArtifacts.find((a) => a.id === artifactId) ?? stageArtifacts.at(-1);
  const fallback: Artifact | undefined =
    delivered && (!stage || stage === 'ready')
      ? {
          id: delivered.id,
          requestId: delivered.id,
          stage: 'ready',
          kind: 'preview',
          label: t('Version {value0}', { value0: versions.indexOf(delivered) + 1 }),
          url: preview(delivered),
        }
      : undefined;
  const previousArtifact = visibleProgress?.artifacts
    .filter(
      (a) =>
        STAGES.findIndex((s) => s.id === a.stage) <=
        STAGES.findIndex((s) => s.id === selectedStage),
    )
    .at(-1);
  const latestImage = artifact ?? fallback ?? previousArtifact;
  const shown =
    latestImage && current?.status === 'failed' && latestImage.kind === 'preview'
      ? { ...latestImage, label: t('Map preview · validation did not pass') }
      : latestImage;
  const celebrating = useDeliveryCelebration(
    p.celebrate,
    currentId,
    current?.status === 'ready' && shown?.stage === 'ready' && shown.url === decodedUrl,
    follow && !comparison,
  );
  const newMap = () => {
    setFollow(true);
    setStage(undefined);
    setArtifactId(undefined);
    setCompare('');
    setCheck(undefined);
  };
  const selectVersion = (value: string) => {
    setInspected(value);
    setFollow(false);
    setStage(undefined);
    setArtifactId(undefined);
    setCompare('');
    setCheck(undefined);
  };
  const parentVersion = versions.find((v) => v.id === p.parent);
  const status = active
    ? active.status === 'uncertain'
      ? active.kind === 'generate'
        ? t('Your credit is reserved while the provider outcome is reconciled.')
        : t('The designer reply needs reconciliation. No map build has started.')
      : (active.error ??
        (active.kind === 'chat'
          ? t('The map designer is replying…')
          : active.status === 'queued'
            ? t('Waiting for service capacity. Your place is saved.')
            : t('Your world is taking shape…')))
    : current?.status === 'failed'
      ? t('Generation stopped. Your failed generation credit is returned.')
      : current?.status === 'ready'
        ? t('Ready for your next adventure')
        : t('A world of possibilities');
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
                  ? 'Map creation ready in Preview.'
                  : 'Map creation needs attention in Preview.',
            }
          : undefined
      }
      attention={active ? 'Working' : current?.status === 'failed' ? 'Needs attention' : undefined}
      conversation={
        <>
          <Conversation
            thread={p.thread}
            active={active?.kind === 'chat'}
            loadEarlier={p.loadEarlier}
            busy={p.busy}
            choose={p.setDraft}
            inspect={selectVersion}
            progress={visibleProgress}
          />
          <div className="ms-compose-area">
            <div className="ms-composer-tools">
              <span>
                {parentVersion
                  ? t('Editing version {value0}', { value0: versions.indexOf(parentVersion) + 1 })
                  : t('New map')}
              </span>
              {parentVersion && (
                <button type="button" onClick={() => p.revise(undefined)}>
                  {t('New map')}
                </button>
              )}
              <details
                className="ms-settings-popover"
                onKeyDown={(e) => {
                  if (e.key === 'Escape') {
                    e.preventDefault();
                    e.stopPropagation();
                    const summary = e.currentTarget.querySelector('summary');
                    e.currentTarget.open = false;
                    requestAnimationFrame(() => summary?.focus());
                  }
                }}
              >
                <summary>
                  <RichMessage
                    source={'{slot0} × {slot1} · {slot2} players'}
                    slots={{
                      slot0: p.settings.width,
                      slot1: p.settings.height,
                      slot2: p.settings.players,
                    }}
                    singular={'{slot0} × {slot1} · {slot2} player'}
                    count={Number(p.settings.players)}
                  />
                </summary>
                <fieldset className="ms-settings" disabled={p.busy || !!active}>
                  <legend>{t('Map settings')}</legend>
                  <div className="ms-settings-grid">
                    {(['width', 'height'] as const).map((axis) => (
                      <label key={axis}>
                        {axis === 'width' ? t('Width') : t('Height')}
                        <select
                          value={p.settings[axis]}
                          onChange={(e) =>
                            p.changeSettings({
                              ...p.settings,
                              [axis]: Number(e.target.value) as StudioSettings['width'],
                            })
                          }
                        >
                          {[128, 256, 512].map((n) => (
                            <option key={n} value={n}>
                              {n}
                            </option>
                          ))}
                        </select>
                      </label>
                    ))}
                    <label>
                      {t('Players')}
                      <select
                        value={p.settings.players}
                        onChange={(e) =>
                          p.changeSettings({ ...p.settings, players: Number(e.target.value) })
                        }
                      >
                        {[2, 3, 4, 5, 6, 7, 8].map((n) => (
                          <option key={n}>{n}</option>
                        ))}
                      </select>
                    </label>
                  </div>
                </fieldset>
              </details>
            </div>
            {p.thread?.brief && (
              <details className="ms-brief">
                <summary>
                  <RichMessage
                    source={'Design brief {slot0}'}
                    slots={{ slot0: <span>{t('Agreed so far')}</span> }}
                  />
                </summary>
                <p>{p.thread.brief}</p>
              </details>
            )}
            <ChatComposer
              value={p.draft}
              onChange={p.setDraft}
              onSend={p.send}
              onBlocked={
                !p.busy && !active && p.wallet?.enabled && !p.wallet.available
                  ? p.openCredits
                  : undefined
              }
              inputRef={composerRef}
              label={t('Describe your map or discuss changes')}
              placeholder={t('Describe your landscape, or dream up a change…')}
              target={
                parentVersion
                  ? t('Editing version {value0}', { value0: versions.indexOf(parentVersion) + 1 })
                  : 'New map'
              }
              disabledReason={
                p.busy || active
                  ? 'Wait for the current request to finish.'
                  : !p.wallet?.enabled
                    ? 'Generation is unavailable. Saved projects remain accessible.'
                    : !p.wallet.available
                      ? 'An available Map credit is needed to chat or build.'
                      : undefined
              }
              pricing="Discussion spends no credits; 1 available Map credit is required. Creation requests build automatically · 1 credit on delivery."
            />
          </div>
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
            <h2>{t('Edit your map')}</h2>
            <p>
              {t(
                'Choose a saved version, then describe the changes in chat. The saved map remains in history.',
              )}
            </p>
            <button
              onClick={() => {
                if (delivered) p.revise(delivered);
                setFocusChat((n) => n + 1);
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
                  selectVersion(v.id);
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
            className="ms-map-pane"
            aria-label={t('Map workshop')}
          >
            <header className="ms-map-heading">
              <h2>{t('Map canvas')}</h2>
              <div className="ms-version-controls">
                {!!generations.length && (
                  <label>
                    <span className="ms-sr">{t('Inspect version ')}</span>
                    <select
                      value={current?.id ?? ''}
                      onChange={(e) => selectVersion(e.target.value)}
                    >
                      {generations.map((r, i) => (
                        <option key={r.id} value={r.id}>
                          {r.status === 'ready'
                            ? t('Version {value0}', {
                                value0: versions.findIndex((v) => v.id === r.id) + 1,
                              })
                            : t('Generation {value0} · {value1}', {
                                value0: i + 1,
                                value1: r.status,
                              })}
                        </option>
                      ))}
                    </select>
                  </label>
                )}
                {versions.length > 1 && delivered && (
                  <label>
                    <span className="ms-sr">{t('Compare with version')}</span>
                    <select
                      value={compare}
                      onChange={(e) => {
                        setCompare(e.target.value);
                        if (e.target.value) {
                          setStage('ready');
                          setArtifactId(undefined);
                          setCheck(undefined);
                          setInspected(current?.id);
                          setFollow(false);
                        }
                      }}
                    >
                      <option value="">{t('Compare…')}</option>
                      {versions
                        .filter((v) => v.id !== delivered.id)
                        .map((v) => (
                          <option key={v.id} value={v.id}>
                            <RichMessage
                              source={'Version {slot0}'}
                              slots={{ slot0: versions.indexOf(v) + 1 }}
                            />
                          </option>
                        ))}
                    </select>
                  </label>
                )}
              </div>
            </header>
            <RequestStatus status={active?.status ?? current?.status ?? 'queued'}>
              <span className={active ? 'ms-live-dot' : ''} aria-hidden="true" />
              {status}
              {!follow && <button onClick={newMap}>{t('Follow latest')}</button>}
            </RequestStatus>
            {current?.status === 'failed' && (
              <div className="ms-banner ms-error">
                {current.error ?? t('We could not finish this map. Your earlier maps are safe.')}
                <button
                  onClick={() => {
                    p.setDraft(
                      t('Please try building the map again and address this issue: {value0}', {
                        value0: current.error ?? 'The last build did not complete.',
                      }),
                    );
                    focusComposer();
                  }}
                >
                  {t('Prepare retry')}
                </button>
              </div>
            )}
            <div className={`ms-viewer-wrap${celebrating ? ' ms-celebrate' : ''}`}>
              {shown ? (
                <>
                  {comparison && delivered ? (
                    <div className="ms-comparison" role="region" aria-label={t('Map comparison')}>
                      <MapViewer
                        key={current?.id}
                        artifact={{
                          id: delivered.id,
                          requestId: delivered.id,
                          stage: 'ready',
                          kind: 'preview',
                          label: t('Version {value0}', { value0: versions.indexOf(delivered) + 1 }),
                          url: preview(delivered),
                        }}
                      />
                      <MapViewer
                        key={comparison.id}
                        artifact={{
                          id: comparison.id,
                          requestId: comparison.id,
                          stage: 'ready',
                          kind: 'preview',
                          label: t('Version {value0}', {
                            value0: versions.indexOf(comparison) + 1,
                          }),
                          url: preview(comparison),
                        }}
                      />
                    </div>
                  ) : (
                    <MapViewer
                      key={current?.id}
                      artifact={shown}
                      onReady={setDecodedUrl}
                      marker={
                        shown.kind === 'preview' || shown.kind === 'categorical'
                          ? check?.location
                          : undefined
                      }
                      dimensions={current?.input.settings}
                    />
                  )}
                </>
              ) : (
                <div className="ms-empty-canvas">
                  <div className="ms-orbit" aria-hidden="true">
                    <span />
                    <img src={ART.explorationFlag} alt="" />
                    <span />
                  </div>
                  <span className="ms-eyebrow">
                    {active?.kind === 'generate'
                      ? t('CREATION IN PROGRESS')
                      : t('IMAGINATION, MEET POSSIBILITY')}
                  </span>
                  <h3>
                    {active?.kind === 'generate'
                      ? t('Something wonderful is taking shape.')
                      : stage
                        ? t('Explore this step.')
                        : t('Your next battlefield starts here.')}
                  </h3>
                  <p>
                    {stage
                      ? t('Images appear here as this stage completes.')
                      : t(
                          'Describe the landscape you have in mind. Watch its journey from first idea to a world you can play.',
                        )}
                  </p>
                  <div className="ms-empty-coordinates" aria-hidden="true">
                    <RichMessage
                      source={'{slot0} × {slot1}  {slot2} UNCHARTED TERRITORY'}
                      slots={{
                        slot0: p.settings.width,
                        slot1: p.settings.height,
                        slot2: <span>{'✧'}</span>,
                      }}
                    />
                  </div>
                </div>
              )}
            </div>
            <details className="ms-build-details">
              <summary>{t('Build details')}</summary>
              <div className="ms-stage-area">
                <GenerationTimeline
                  stages={stages}
                  artifacts={visibleProgress?.artifacts ?? []}
                  selectedStage={selectedStage}
                  enabled={!!current}
                  select={(id) => {
                    setStage(id);
                    setArtifactId(undefined);
                    setInspected(current?.id);
                    setFollow(false);
                    setCheck(undefined);
                    setCompare('');
                  }}
                />
                {stage && stages.find((s) => s.id === selectedStage)?.detail && (
                  <p className="ms-stage-note">
                    {stages.find((s) => s.id === selectedStage)?.detail}
                  </p>
                )}
                {stageArtifacts.length > 1 && (
                  <details className="ms-artifacts">
                    <summary>
                      <RichMessage
                        source={'{slot0} · {count} images'}
                        singular={'{slot0} · {count} image'}
                        count={stageArtifacts.length}
                        slots={{
                          slot0: stages.find((s) => s.id === selectedStage)?.label,
                        }}
                      />
                    </summary>
                    <div>
                      {stageArtifacts.map((a) => (
                        <button
                          key={a.id}
                          aria-pressed={shown?.id === a.id}
                          onClick={() => {
                            setArtifactId(a.id);
                            setStage(a.stage);
                            setInspected(current?.id);
                            setFollow(false);
                          }}
                        >
                          <img src={a.url} alt="" />
                          {artifactLabel(a)}
                        </button>
                      ))}
                    </div>
                  </details>
                )}
                {progressError && <p className="ms-stage-note">{progressError}</p>}
                {visibleProgress?.historical && (
                  <p className="ms-stage-note">
                    {t(
                      'This older generation has limited stage history. Available images are preserved.',
                    )}
                  </p>
                )}
                {!!visibleProgress?.checks.length && (
                  <ValidationPanel
                    checks={visibleProgress.checks}
                    checking={stages.find((s) => s.id === 'checks')?.status === 'running'}
                    selected={check?.id}
                    select={(value) => {
                      setCompare('');
                      setCheck(value);
                      setArtifactId(undefined);
                      setStage(
                        visibleProgress.artifacts.some((a) => a.stage === 'ready')
                          ? 'ready'
                          : 'build',
                      );
                      setInspected(current?.id);
                      setFollow(false);
                    }}
                  />
                )}
              </div>
            </details>
            {delivered && (
              <div className="ms-delivery">
                <div>
                  <strong>{t('Made for your next match.')}</strong>
                  <span>
                    <RichMessage
                      source={'{slot0} × {slot1} · {slot2} players · Private until published'}
                      slots={{
                        slot0: delivered.input.settings.width,
                        slot1: delivered.input.settings.height,
                        slot2: delivered.input.settings.players,
                      }}
                      singular={'{slot0} × {slot1} · {slot2} player · Private until published'}
                      count={Number(delivered.input.settings.players)}
                    />
                  </span>
                </div>
                <div className="ms-delivery-actions">
                  <button
                    className="primary"
                    disabled={p.busy}
                    onClick={() => p.versionAction('host', delivered)}
                  >
                    <RichMessage
                      source={'Play {slot0}'}
                      slots={{ slot0: <span aria-hidden="true">{'↗'}</span> }}
                    />
                  </button>
                  <button
                    disabled={p.busy || !!active}
                    onClick={() => {
                      p.revise(delivered);
                      focusComposer();
                    }}
                  >
                    {t('Edit this version')}
                  </button>
                  <button
                    disabled={p.busy || !!active}
                    onClick={() => {
                      p.revise(undefined);
                      focusComposer();
                    }}
                  >
                    {t('New map')}
                  </button>
                  <a
                    className="btn"
                    href={`/api/v1/maps/${delivered.map_id}/versions/${delivered.map_hash}/file`}
                  >
                    {t('Download')}
                  </a>

                  <details>
                    <summary>{t('More')}</summary>
                    <button disabled={p.busy} onClick={() => p.versionAction('publish', delivered)}>
                      {t('Publish this version')}
                    </button>
                    <Link to={`/maps/${delivered.map_id}`}>{t('Map details')}</Link>
                  </details>
                </div>
              </div>
            )}
          </section>
        </>
      }
    />
  );
}
