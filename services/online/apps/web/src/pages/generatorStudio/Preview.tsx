import { t, useLocale, RichMessage } from '../../i18n.tsx';
import { useEffect, useRef, useState } from 'react';
import type { GeneratorSettings } from '@glob2/protocol';
export interface GeneratorRun {
  runId: string;
  revision: number;
  source: string;
  draftHash: string;
  settings: GeneratorSettings;
}
type PreviewStatus =
  | { kind: 'loading' | 'timeout' }
  | { kind: 'generated'; revision: number; seed: GeneratorSettings['seed']; seconds: string }
  | { kind: 'progress'; revision: number; tick: number }
  | { kind: 'failure' | 'error' | 'complete'; raw: string };

function primaryStatus(status: PreviewStatus, locale: string): string {
  switch (status.kind) {
    case 'loading':
      return t('Loading engine…');
    case 'timeout':
      return t('Engine launch timed out. Stop and retry the preview.');
    case 'generated':
      return t('Revision {value0} · seed {value1} · {value2} seconds', {
        value0: status.revision,
        value1: status.seed,
        value2: status.seconds,
      });
    case 'progress':
      return t('Revision {value0} · tick {value1} · live', {
        value0: status.revision,
        value1: status.tick,
      });
    case 'failure':
    case 'error':
      return locale === 'bg' || locale === 'th'
        ? t('Something went wrong. Please try again.')
        : status.raw;
    case 'complete':
      return locale === 'bg' || locale === 'th' ? t('Finished') : status.raw;
  }
}

export default function Preview({
  run,
  onResult,
}: {
  run: GeneratorRun;
  onResult: (text: string) => void;
}) {
  const locale = useLocale();
  const frame = useRef<HTMLIFrameElement>(null),
    callback = useRef(onResult);
  const [status, setStatus] = useState<PreviewStatus>({ kind: 'loading' }),
    [ready, setReady] = useState(false),
    [playable, setPlayable] = useState(false),
    [playing, setPlaying] = useState(false),
    [logs, setLogs] = useState<string[]>([]),
    [report, setReport] = useState<Record<string, unknown>>();
  useEffect(() => {
    callback.current = onResult;
  }, [onResult]);
  const post = (type: string) =>
    frame.current?.contentWindow?.postMessage(
      {
        channel: 'glob2-generator-studio',
        version: 1,
        type,
        runId: run.runId,
        revision: run.revision,
      },
      location.origin,
    );
  useEffect(() => {
    let closed = false;
    let generated: Record<string, unknown> | undefined;
    const diagnostics: string[] = [];
    const timeout = setTimeout(() => {
      if (!closed) {
        setStatus({ kind: 'timeout' });
        callback.current(
          JSON.stringify({
            revision: run.revision,
            settings: run.settings,
            error: t('launch timeout'),
          }),
        );
      }
    }, 90000);
    const listener = (event: MessageEvent) => {
      const m = event.data;
      if (
        closed ||
        event.origin !== location.origin ||
        event.source !== frame.current?.contentWindow ||
        !m ||
        m.channel !== 'glob2-generator-studio' ||
        m.version !== 1 ||
        m.runId !== run.runId ||
        m.revision !== run.revision
      )
        return;
      if (m.type === 'ready')
        frame.current?.contentWindow?.postMessage(
          {
            channel: 'glob2-generator-studio',
            version: 1,
            type: 'launch',
            ...run,
          },
          location.origin,
        );
      if (
        m.type === 'generated' &&
        typeof m.success === 'boolean' &&
        typeof m.seconds === 'number' &&
        Number.isFinite(m.seconds) &&
        m.seconds >= 0 &&
        (m.diagnostic === undefined || typeof m.diagnostic === 'string') &&
        (!m.success ||
          (typeof m.packageHash === 'string' &&
            /^[0-9a-f]{64}$/.test(m.packageHash) &&
            typeof m.worldFingerprint === 'string' &&
            /^[0-9a-f]{64}$/.test(m.worldFingerprint) &&
            typeof m.simVersion === 'string' &&
            m.simVersion.length <= 128 &&
            Number.isInteger(m.checksum) &&
            m.checksum >= 0 &&
            m.checksum <= 4294967295 &&
            Array.isArray(m.telemetry) &&
            m.telemetry.length <= 40))
      ) {
        clearTimeout(timeout);
        generated = m;
        setReady(m.success);
        setReport(m);
        setPlayable(m.playable === true);
        setStatus(
          m.success
            ? {
                kind: 'generated',
                revision: run.revision,
                seed: run.settings.seed,
                seconds: Number(m.seconds).toFixed(2),
              }
            : { kind: 'failure', raw: String(m.diagnostic || 'Generation failed').slice(0, 2000) },
        );
        callback.current(
          JSON.stringify({
            revision: run.revision,
            draftHash: run.draftHash,
            settings: run.settings,
            ...m,
          }).slice(0, 16000),
        );
      }
      if (
        m.type === 'progress' &&
        Number.isInteger(m.tick) &&
        m.tick >= 0 &&
        m.tick <= 4294967295
      ) {
        clearTimeout(timeout);
        setStatus({ kind: 'progress', revision: run.revision, tick: Number(m.tick) || 0 });
      }
      if (m.type === 'diagnostic' && typeof m.text === 'string' && diagnostics.length < 40) {
        diagnostics.push(m.text.slice(0, 2000));
        setLogs([...diagnostics]);
      }
      if (m.type === 'error' || m.type === 'complete') {
        clearTimeout(timeout);
        const raw = String(m.text ?? m.result ?? 'Finished').slice(0, 2000);
        setStatus({ kind: m.type, raw });
        callback.current(
          JSON.stringify({
            revision: run.revision,
            draftHash: run.draftHash,
            settings: run.settings,
            ...generated,
            ...m,
            logs: diagnostics,
          }).slice(0, 16000),
        );
      }
    };
    window.addEventListener('message', listener);
    const element = frame.current;
    const win = element?.contentWindow;
    return () => {
      closed = true;
      clearTimeout(timeout);
      window.removeEventListener('message', listener);
      if (element?.isConnected)
        win?.postMessage(
          {
            channel: 'glob2-generator-studio',
            version: 1,
            type: 'stop',
            runId: run.runId,
            revision: run.revision,
          },
          location.origin,
        );
    };
  }, [run]);
  // Derive display from retained event data so locale changes never restart the
  // engine and never leave a previous locale's primary diagnostic on screen.
  const terminal = status.kind === 'error' || status.kind === 'complete';
  const displayLogs =
    (locale === 'bg' || locale === 'th') && terminal ? [...logs, status.raw] : logs;
  return (
    <div className="as-playtest">
      <p role="status">{primaryStatus(status, locale)}</p>
      <button
        disabled={!ready || !playable || playing}
        onClick={() => {
          post('watch');
          setPlaying(true);
        }}
      >
        {t('Watch AI play')}
      </button>
      {ready && !playable && <p>{t('Editor-only terrain cannot launch a colony game.')}</p>}
      <iframe
        ref={frame}
        title={t('Generator preview revision {value0}', {
          value0: run.revision,
        })}
        src={`/play/generator-studio.html?run=${run.runId}&revision=${run.revision}`}
        allow="cross-origin-isolated; fullscreen"
      />
      {report && (
        <details open={!ready}>
          <summary>{t('Generation report')}</summary>
          <pre>
            {JSON.stringify(
              {
                revision: run.revision,
                settings: run.settings,
                draftHash: run.draftHash,
                ...report,
              },
              null,
              2,
            ).slice(0, 16000)}
          </pre>
        </details>
      )}
      <details>
        <summary>
          <RichMessage
            source={'Runtime diagnostics ({slot0})'}
            slots={{ slot0: displayLogs.length }}
          />
        </summary>
        <pre>{displayLogs.join('\n')}</pre>
      </details>
    </div>
  );
}
