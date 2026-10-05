import { useEffect, useRef, useState, type CSSProperties } from 'react';
import type { MusicRelease, MusicStudioSettings, MusicStudioProgress } from '@glob2/protocol';
import { request } from '../../api.ts';
import { MusicPlayer } from '../../music/Player.tsx';
import { useDeliveryCelebration } from '../studio/useDeliveryCelebration.ts';
import { MUSIC_IDEAS } from '../MusicStudioLanding.tsx';
import { ROOT, type Thread, type Wallet, type Delivered } from './types.ts';
interface Props {
  id?: string;
  thread?: Thread;
  wallet?: Wallet;
  busy: boolean;
  draft: string;
  setDraft: (s: string) => void;
  send: () => void;
  generate: () => void;
  settings: MusicStudioSettings;
  changeSettings: (s: MusicStudioSettings) => void;
  parent?: string;
  revise: (v: Delivered | undefined) => void;
  revision: number;
  celebrate?: string;
  loadEarlier: () => void;
  versionAction: (v: Delivered, license: string) => void;
}
export function MusicWorkspace(p: Props) {
  const [split, setSplit] = useState(36),
    [pane, setPane] = useState('chat'),
    [follow, setFollow] = useState(true),
    [selected, setSelected] = useState('');
  const [progress, setProgress] = useState<MusicStudioProgress>(),
    [release, setRelease] = useState<MusicRelease>(),
    [error, setError] = useState('');
  const [compare, setCompare] = useState(''),
    [preview, setPreview] = useState('');
  const [initialPosition, setInitialPosition] = useState(0);
  const [cancelling, setCancelling] = useState(false);
  const position = useRef(0),
    previousFrames = useRef(0),
    previousTimeline = useRef<string | undefined>(undefined);
  const active = p.thread?.requests.find((r) => !['ready', 'failed'].includes(r.status));
  const generations = p.thread?.requests.filter((r) => r.kind === 'generate') ?? [];
  const versions = generations.filter(
    (r): r is Delivered => r.status === 'ready' && !!r.release_id && !!r.input.settings,
  );
  const current = generations.find((r) => r.id === (follow ? generations.at(-1)?.id : selected));
  const displayed = compare
    ? versions.find((v) => v.id === compare)
    : versions.find((v) => v.id === current?.id);
  const celebration = useDeliveryCelebration(p.celebrate, current?.id, !!displayed, follow);
  const currentId = current?.id,
    releaseId = displayed?.release_id;
  useEffect(() => {
    if (!p.id || !currentId) return;
    const abort = new AbortController();
    void request<MusicStudioProgress>(
      'GET',
      `${ROOT}/threads/${p.id}/requests/${currentId}/progress`,
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
  }, [p.id, currentId, p.revision]);
  useEffect(() => {
    if (!releaseId) return;
    const abort = new AbortController();
    void request<MusicRelease>('GET', `/api/v1/music/${releaseId}`, {
      signal: abort.signal,
    })
      .then((value) => {
        if (abort.signal.aborted) return;
        if (
          value.frames !== previousFrames.current ||
          !value.timelineId ||
          value.timelineId !== previousTimeline.current
        )
          position.current = 0;
        previousTimeline.current = value.timelineId;
        previousFrames.current = value.frames;
        setInitialPosition(position.current);
        setPreview('');
        setRelease(value);
      })
      .catch((e) => {
        if (!abort.signal.aborted) setError(String(e));
      });
    return () => abort.abort();
  }, [releaseId]);
  const shown = progress?.requestId === current?.id ? progress : undefined;
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
    <>
      <nav className="mu-mobile-tabs" aria-label="Workspace pane">
        <button aria-pressed={pane === 'chat'} onClick={() => setPane('chat')}>
          Conversation
        </button>
        <button aria-pressed={pane === 'listen'} onClick={() => setPane('listen')}>
          Listen & inspect
        </button>
      </nav>
      <div
        className="mu-workspace"
        data-pane={pane}
        style={{ '--chat-width': `${split}%` } as CSSProperties}
      >
        <section className="mu-chat">
          <div className="mu-chat-heading">
            <span className="ms-eyebrow">YOUR COMPOSER</span>
            <h2>{p.parent ? 'Refine your soundtrack' : 'Start with a feeling'}</h2>
          </div>
          <div
            className="mu-messages"
            role="log"
            aria-label="Music composition conversation"
            aria-live="polite"
          >
            {(p.thread?.history?.messagesBefore || p.thread?.history?.requestsBefore) && (
              <button disabled={p.busy} onClick={p.loadEarlier}>
                Load earlier conversation and versions
              </button>
            )}
            {!p.thread?.messages.length && (
              <>
                <p>
                  Tell me about the world you want to hear. We’ll shape the melody, instruments and
                  three moods together.
                </p>
                {MUSIC_IDEAS.map(([title, text]) => (
                  <button key={title} className="mu-prompt" onClick={() => p.setDraft(text)}>
                    {title} ↗
                  </button>
                ))}
              </>
            )}
            {p.thread?.messages.map((m) => (
              <article key={m.id} className={`mu-message ${m.role}`}>
                <small>{m.role === 'user' ? 'YOU' : 'COMPOSER'}</small>
                <p>{m.text}</p>
              </article>
            ))}
            {active && (
              <p className="mu-working" role="status">
                {active.status === 'uncertain'
                  ? 'The provider outcome needs reconciliation. Your credit remains reserved.'
                  : 'Your composer is working…'}
              </p>
            )}
          </div>
          <form
            className="mu-compose"
            onSubmit={(e) => {
              e.preventDefault();
              if (canSend && p.draft.trim()) p.send();
            }}
          >
            {p.parent && (
              <p>
                Revising version {versions.findIndex((v) => v.id === p.parent) + 1}{' '}
                <button type="button" onClick={() => p.revise(undefined)}>
                  Start fresh
                </button>
              </p>
            )}
            <label htmlFor="music-prompt">Your idea or next change</label>
            <textarea
              id="music-prompt"
              rows={4}
              maxLength={8000}
              value={p.draft}
              onChange={(e) => p.setDraft(e.target.value)}
              placeholder="Keep the melody, but make calm more spacious…"
            />
            <button type="submit" disabled={!canSend || !p.draft.trim()}>
              Send to composer ↑
            </button>
            <div className="mu-settings">
              <label>
                Sound palette
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
                  <option value="acoustic-v1">Acoustic · woodwinds, strings & folk</option>
                  <option value="synth-v1">Synth · warm bells, pads & pulse</option>
                </select>
              </label>
              <label>
                Seed
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
              License for this version
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
                <option value="CC-BY-4.0">CC BY 4.0</option>
                <option value="CC0-1.0">CC0 1.0</option>
                <option value="CC-BY-SA-4.0">CC BY-SA 4.0</option>
              </select>
            </label>
            <button
              type="button"
              className="primary mu-generate"
              disabled={!canSend || !p.thread?.messages.length || !!p.draft.trim()}
              onClick={p.generate}
            >
              {p.parent ? 'Create revision' : 'Compose soundtrack'} · 1 credit
            </button>
            <small>Discuss your changes first. A delivered set includes all three moods.</small>
          </form>
        </section>
        <div
          className="mu-divider"
          role="separator"
          aria-label="Resize conversation pane"
          aria-orientation="vertical"
          aria-valuemin={25}
          aria-valuemax={55}
          aria-valuenow={split}
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
              e.preventDefault();
              setSplit((n) => Math.max(25, Math.min(55, n + (e.key === 'ArrowLeft' ? -2 : 2))));
            }
          }}
          onPointerDown={(e) => e.currentTarget.setPointerCapture(e.pointerId)}
          onPointerMove={(e) => {
            if (e.currentTarget.hasPointerCapture(e.pointerId)) {
              const rect = e.currentTarget.parentElement?.getBoundingClientRect();
              if (!rect) return;
              setSplit(Math.max(25, Math.min(55, ((e.clientX - rect.left) / rect.width) * 100)));
            }
          }}
        />
        <section className={`mu-inspector ${celebration ? 'mu-reveal' : ''}`}>
          <header className="mu-inspector-heading">
            <div>
              <span className="ms-eyebrow">THE LISTENING ROOM</span>
              <h2>
                {current
                  ? `Version ${generations.indexOf(current) + 1}`
                  : 'Your music takes shape here'}
              </h2>
            </div>
            <button
              aria-pressed={follow}
              onClick={() => {
                setFollow(true);
                setCompare('');
              }}
            >
              Follow live
            </button>
          </header>
          {!!generations.length && (
            <nav className="mu-versions" aria-label="Music revisions">
              {generations.map((v, i) => (
                <button
                  key={v.id}
                  aria-pressed={current?.id === v.id}
                  onClick={() => {
                    setSelected(v.id);
                    setFollow(false);
                    setCompare('');
                    setPreview('');
                  }}
                >
                  V{i + 1} <small>{v.status}</small>
                </button>
              ))}
            </nav>
          )}
          {error && <p role="alert">{error}</p>}
          {current?.error && <p role="alert">{current.error}</p>}
          {!current && (
            <div className="mu-empty">
              <span aria-hidden="true">♫</span>
              <h3>One melody. Three moods.</h3>
              <p>Follow the composition, hear each candidate, and see what the checks find.</p>
              <div className="mu-mood-labels">
                <span>Calm</span>
                <span>Building</span>
                <span>Combat</span>
              </div>
            </div>
          )}
          {shown && (
            <ol className="mu-stages" aria-label="Generation stages">
              {shown.stages.map((stage) => (
                <li key={stage.id} data-status={stage.status}>
                  <span>
                    {stage.status === 'complete' ? '✓' : stage.status === 'running' ? '◉' : '○'}
                  </span>
                  <div>
                    <strong>{stage.label}</strong>
                    <small>{stage.detail ?? stage.status}</small>
                  </div>
                </li>
              ))}
            </ol>
          )}
          {active?.kind === 'generate' && current?.id === active.id && (
            <button
              disabled={cancelling || ['dispatched', 'uncertain'].includes(active.status)}
              onClick={() => void cancel()}
            >
              Cancel generation
            </button>
          )}
          {playable && !preview && (
            <>
              <div className="mu-audition-header">
                <h3>{compare ? 'Comparing saved version' : 'Ready to listen'}</h3>
                <select
                  aria-label="Compare revision"
                  value={compare}
                  onChange={(e) => setCompare(e.target.value)}
                >
                  <option value="">Selected version</option>
                  {versions
                    .filter((v) => v.id !== current?.id)
                    .map((v) => (
                      <option key={v.id} value={v.id}>
                        Compare V{generations.findIndex((g) => g.id === v.id) + 1}
                      </option>
                    ))}
                </select>
              </div>
              <MusicPlayer
                key={playable.id}
                release={playable}
                initialPosition={initialPosition}
                onPosition={(value) => {
                  position.current = value;
                }}
              />
              <div className="mu-actions">
                <a className="btn" href={`/api/v1/music/${playable.id}/download`}>
                  Download set
                </a>
                <button
                  disabled={!!active}
                  onClick={() => {
                    if (displayed) p.revise(displayed);
                    setPane('chat');
                  }}
                >
                  Revise this version
                </button>
                <span>License: {playable.metadata.license}</span>
                <button
                  disabled={p.busy || playable.status === 'published'}
                  onClick={() => {
                    if (displayed) p.versionAction(displayed, playable.metadata.license);
                  }}
                >
                  {playable.status === 'published' ? 'Published' : 'Publish to music library'}
                </button>
              </div>
              <p className="mu-private">
                {playable.status === 'published'
                  ? 'This version is public.'
                  : 'Saved privately. Publishing shares only this version.'}{' '}
                AI composition is disclosed.
              </p>
            </>
          )}
          {shown && !!shown.artifacts.length && (
            <details className="mu-artifacts">
              <summary>Candidate previews & reports</summary>
              {shown.artifacts.map((a) =>
                a.kind === 'preview' ? (
                  <button key={a.id} onClick={() => setPreview(a.url)}>
                    {a.label}
                  </button>
                ) : (
                  <a key={a.id} href={a.url} target="_blank" rel="noreferrer">
                    {a.label}
                  </a>
                ),
              )}
            </details>
          )}
          {preview && (
            <div className="mu-candidate">
              <p>Candidate preview · may need repairs</p>
              <audio key={preview} src={preview} controls />
              <button onClick={() => setPreview('')}>Return to final set</button>
            </div>
          )}
          {shown && !!shown.notes.length && (
            <details className="mu-notes">
              <summary>Composer’s progress</summary>
              {shown.notes.map((n, i) => (
                <p key={i}>
                  <small>Candidate {n.attempt}</small> {n.text}
                </p>
              ))}
            </details>
          )}
          {shown && !!shown.checks.length && (
            <section className="mu-checks">
              <h3>Validation, in detail</h3>
              <p>
                These checks catch measurable defects. Your ears decide whether the music feels
                right.
              </p>
              {shown.checks.map((c) => (
                <details key={c.id} data-status={c.status}>
                  <summary>
                    <span className={`mu-status ${c.status}`}>{c.status}</span>
                    <strong>{c.label}</strong>
                    <small>Candidate {c.attempt}</small>
                  </summary>
                  {c.detail && <p>{c.detail}</p>}
                  {c.measures.map((m, i) => (
                    <div className="mu-measure" key={`${m.name}:${i}`}>
                      <strong>{m.name}</strong>
                      <span>
                        {m.status} ·{' '}
                        {typeof m.value === 'object'
                          ? JSON.stringify(m.value)
                          : String(m.value ?? '—')}{' '}
                        {m.unit}
                      </span>
                      <small>{m.threshold}</small>
                      <p>{m.detail}</p>
                    </div>
                  ))}
                </details>
              ))}
            </section>
          )}
        </section>
      </div>
    </>
  );
}
