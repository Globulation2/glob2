import { useCallback, useEffect, useRef, useState, type CSSProperties } from 'react';
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
  versionAction: (action: 'host' | 'publish', v: Delivered) => void;
}
export function StudioWorkspace(p: Props) {
  const [pane, setPane] = useState<'chat' | 'map'>('chat');
  const [split, updateSplit] = useState(() => {
    const saved = Number(localStorage.getItem('map-studio-split') ?? 50);
    return Number.isFinite(saved) ? Math.max(35, Math.min(65, saved)) : 50;
  });
  const splitRef = useRef<HTMLDivElement>(null);
  const setSplit = useCallback((value: number) => {
    const width = splitRef.current?.clientWidth ?? 1000;
    if (
      width < 649 ||
      (typeof matchMedia === 'function' && matchMedia('(max-width: 899px)').matches)
    )
      return;
    const minimum = Math.max(35, (320 / width) * 100);
    const next = Math.max(minimum, Math.min(Math.min(65, ((width - 329) / width) * 100), value));
    updateSplit(next);
    localStorage.setItem('map-studio-split', String(next));
  }, []);
  const composerRef = useRef<HTMLTextAreaElement>(null);
  const focusComposer = () => {
    setPane('chat');
    requestAnimationFrame(() => composerRef.current?.focus());
  };
  useEffect(() => {
    const element = splitRef.current;
    if (!element || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(() => setSplit(split));
    observer.observe(element);
    return () => observer.disconnect();
    // The observer re-clamps the current split when navigation or the viewport changes.
  }, [split, setSplit]);
  const [follow, setFollow] = useState(true);
  const [inspected, setInspected] = useState<string>();
  const [stage, setStage] = useState<StageId>();
  const [artifactId, setArtifactId] = useState<string>();
  const [compare, setCompare] = useState('');
  const [check, setCheck] = useState<Check>();
  const [decodedUrl, setDecodedUrl] = useState<string>();
  const active = p.thread?.requests.find((r) => !['ready', 'failed'].includes(r.status));
  const generations = p.thread?.requests.filter((r) => r.kind === 'generate') ?? [];
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
          label: `Version ${versions.indexOf(delivered) + 1}`,
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
      ? { ...latestImage, label: 'Map preview · validation did not pass' }
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
    setPane('map');
    const version = versions.find((v) => v.id === value);
    if (version) p.revise(version);
    setInspected(value);
    setFollow(false);
    setStage(undefined);
    setArtifactId(undefined);
    setCompare('');
    setCheck(undefined);
  };
  const parentVersion = versions.find((v) => v.id === p.parent);
  const disabled = p.busy || !!active || !p.wallet?.enabled || !p.wallet.available;
  const status = active
    ? active.status === 'uncertain'
      ? active.kind === 'generate'
        ? 'Your credit is reserved while the provider outcome is reconciled.'
        : 'The designer reply needs reconciliation. No map build has started.'
      : (active.error ??
        (active.kind === 'chat'
          ? 'The map designer is replying…'
          : active.status === 'queued'
            ? 'Waiting for service capacity. Your place is saved.'
            : 'Your world is taking shape…'))
    : current?.status === 'failed'
      ? 'Generation stopped. Your failed generation credit is returned.'
      : current?.status === 'ready'
        ? 'Ready for your next adventure'
        : 'A world of possibilities';
  return (
    <>
      <div className="ms-mobile-tabs" role="group" aria-label="Studio view">
        <button aria-pressed={pane === 'chat'} onClick={() => setPane('chat')}>
          Chat
        </button>
        <button aria-pressed={pane === 'map'} onClick={() => setPane('map')}>
          Map{' '}
          {active?.kind === 'generate' ? (
            <span className="ms-live-dot" />
          ) : current?.status === 'ready' ? (
            '✓'
          ) : (
            ''
          )}
        </button>
      </div>
      <div
        className="ms-workspace"
        ref={splitRef}
        data-pane={pane}
        style={{ '--chat-width': `${split}%` } as CSSProperties}
      >
        <section className="ms-conversation" aria-label="Conversation">
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
                  ? `Editing version ${versions.indexOf(parentVersion) + 1}`
                  : 'New map'}
              </span>
              {parentVersion && (
                <button type="button" onClick={() => p.revise(undefined)}>
                  New map
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
                  {p.settings.width} × {p.settings.height} · {p.settings.players} players
                </summary>
                <fieldset className="ms-settings" disabled={p.busy || !!active}>
                  <legend>Map settings</legend>
                  <div className="ms-settings-grid">
                    {(['width', 'height'] as const).map((axis) => (
                      <label key={axis}>
                        {axis === 'width' ? 'Width' : 'Height'}
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
                      Players
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
                  Design brief <span>Agreed so far</span>
                </summary>
                <p>{p.thread.brief}</p>
              </details>
            )}
            <form
              className="ms-composer"
              onSubmit={(e) => {
                e.preventDefault();
                if (p.draft.trim() && !disabled) p.send();
              }}
            >
              <label className="ms-sr" htmlFor="studio-message">
                Describe your map or discuss changes
              </label>
              <textarea
                ref={composerRef}
                id="studio-message"
                value={p.draft}
                rows={2}
                maxLength={8000}
                placeholder="Describe your landscape, or dream up a change…"
                onChange={(e) => {
                  p.setDraft(e.target.value);
                  e.target.style.height = 'auto';
                  e.target.style.height = `${Math.min(e.target.scrollHeight, 150)}px`;
                }}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' && !e.shiftKey && !e.nativeEvent.isComposing) {
                    e.preventDefault();
                    if (p.draft.trim() && !disabled) p.send();
                  }
                }}
              />
              <div className="ms-compose-foot">
                <span>Enter to send · Shift + Enter for a new line</span>
                <button
                  type="submit"
                  className="ms-send"
                  disabled={disabled || !p.draft.trim()}
                  aria-label="Send message"
                >
                  ↑
                </button>
              </div>
            </form>
            <p className="ms-composer-note">
              Messages are free. If your request builds a map, it uses 1 credit on delivery.
              {p.wallet &&
                !p.wallet.available &&
                !active &&
                ' An available credit is needed to chat or build.'}
            </p>
          </div>
        </section>
        <div
          className="ms-separator"
          role="separator"
          tabIndex={0}
          aria-label="Resize conversation"
          aria-orientation="vertical"
          aria-valuemin={35}
          aria-valuemax={65}
          aria-valuenow={Math.round(split)}
          onKeyDown={(e) => {
            if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) {
              e.preventDefault();
              setSplit(
                e.key === 'Home'
                  ? 35
                  : e.key === 'End'
                    ? 65
                    : split + (e.key === 'ArrowLeft' ? -2 : 2),
              );
            }
          }}
          onPointerDown={(e) => {
            e.currentTarget.setPointerCapture(e.pointerId);
          }}
          onPointerMove={(e) => {
            if (e.currentTarget.hasPointerCapture(e.pointerId) && splitRef.current) {
              const r = splitRef.current.getBoundingClientRect();
              setSplit(((e.clientX - r.left) / r.width) * 100);
            }
          }}
          onPointerUp={(e) => e.currentTarget.releasePointerCapture(e.pointerId)}
        >
          <span />
        </div>
        <section className="ms-map-pane" aria-label="Map workshop">
          <header className="ms-map-heading">
            <strong>Map canvas</strong>
            <div className="ms-version-controls">
              {!!generations.length && (
                <label>
                  <span className="ms-sr">Inspect version</span>
                  <select value={current?.id ?? ''} onChange={(e) => selectVersion(e.target.value)}>
                    {generations.map((r, i) => (
                      <option key={r.id} value={r.id}>
                        {r.status === 'ready'
                          ? `Version ${versions.findIndex((v) => v.id === r.id) + 1}`
                          : `Generation ${i + 1} · ${r.status}`}
                      </option>
                    ))}
                  </select>
                </label>
              )}
              {versions.length > 1 && delivered && (
                <label>
                  <span className="ms-sr">Compare with version</span>
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
                    <option value="">Compare…</option>
                    {versions
                      .filter((v) => v.id !== delivered.id)
                      .map((v) => (
                        <option key={v.id} value={v.id}>
                          Version {versions.indexOf(v) + 1}
                        </option>
                      ))}
                  </select>
                </label>
              )}
            </div>
          </header>
          <div className="ms-map-status" role="status">
            <span className={active ? 'ms-live-dot' : ''} aria-hidden="true" />
            {status}
            {!follow && <button onClick={newMap}>Follow live ↗</button>}
          </div>
          {current?.status === 'failed' && (
            <div className="ms-banner ms-error">
              {current.error ?? 'We could not finish this map. Your earlier maps are safe.'}
              <button
                onClick={() => {
                  p.setDraft(
                    `Please try building the map again and address this issue: ${current.error ?? 'The last build did not complete.'}`,
                  );
                  focusComposer();
                }}
              >
                Prepare retry
              </button>
            </div>
          )}
          <div className={`ms-viewer-wrap${celebrating ? ' ms-celebrate' : ''}`}>
            {shown ? (
              <>
                {comparison && delivered ? (
                  <div className="ms-comparison" role="region" aria-label="Map comparison">
                    <MapViewer
                      key={current?.id}
                      artifact={{
                        id: delivered.id,
                        requestId: delivered.id,
                        stage: 'ready',
                        kind: 'preview',
                        label: `Version ${versions.indexOf(delivered) + 1}`,
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
                        label: `Version ${versions.indexOf(comparison) + 1}`,
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
                    ? 'CREATION IN PROGRESS'
                    : 'IMAGINATION, MEET POSSIBILITY'}
                </span>
                <h3>
                  {active?.kind === 'generate'
                    ? 'Something wonderful is taking shape.'
                    : stage
                      ? 'Explore this step.'
                      : 'Your next battlefield starts here.'}
                </h3>
                <p>
                  {stage
                    ? 'Images appear here as this stage completes.'
                    : 'Describe the landscape you have in mind. Watch its journey from first idea to a world you can play.'}
                </p>
                <div className="ms-empty-coordinates" aria-hidden="true">
                  {p.settings.width} × {p.settings.height} <span>✧</span> UNCHARTED TERRITORY
                </div>
              </div>
            )}
          </div>
          <details className="ms-build-details">
            <summary>Build details</summary>
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
                    {stages.find((s) => s.id === selectedStage)?.label} · {stageArtifacts.length}{' '}
                    {stageArtifacts.length === 1 ? 'image' : 'images'}
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
                        {a.label}
                      </button>
                    ))}
                  </div>
                </details>
              )}
              {progressError && <p className="ms-stage-note">{progressError}</p>}
              {visibleProgress?.historical && (
                <p className="ms-stage-note">
                  This older generation has limited stage history. Available images are preserved.
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
                <strong>Made for your next match.</strong>
                <span>
                  {delivered.input.settings.width} × {delivered.input.settings.height} ·{' '}
                  {delivered.input.settings.players} players · Private until published
                </span>
              </div>
              <div className="ms-delivery-actions">
                <button
                  className="primary"
                  disabled={p.busy}
                  onClick={() => p.versionAction('host', delivered)}
                >
                  Play <span aria-hidden="true">↗</span>
                </button>
                <button
                  disabled={p.busy || !!active}
                  onClick={() => {
                    p.revise(delivered);
                    focusComposer();
                  }}
                >
                  Edit this version
                </button>
                <button
                  disabled={p.busy || !!active}
                  onClick={() => {
                    p.revise(undefined);
                    focusComposer();
                  }}
                >
                  New map
                </button>
                <a
                  className="btn"
                  href={`/api/v1/maps/${delivered.map_id}/versions/${delivered.map_hash}/file`}
                >
                  Download
                </a>

                <details>
                  <summary>More</summary>
                  <button disabled={p.busy} onClick={() => p.versionAction('publish', delivered)}>
                    Publish this version
                  </button>
                  <Link to={`/maps/${delivered.map_id}`}>Map details</Link>
                </details>
              </div>
            </div>
          )}
        </section>
      </div>
    </>
  );
}
