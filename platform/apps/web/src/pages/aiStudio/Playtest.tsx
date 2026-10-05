import { useEffect, useRef, useState } from 'react';
export interface Run {
  runId: string;
  revision: number;
  source: string;
  seed: number;
  opponent: string;
}
export default function Playtest({
  run,
  onResult,
}: {
  run: Run;
  onResult: (text: string) => void;
}) {
  const frame = useRef<HTMLIFrameElement>(null),
    callback = useRef(onResult);
  useEffect(() => {
    callback.current = onResult;
  }, [onResult]);
  const [status, setStatus] = useState('Loading game…'),
    [diagnostics, setDiagnostics] = useState<string[]>([]);
  useEffect(() => {
    const abort = new AbortController();
    const contentWindow = frame.current?.contentWindow;
    let launching = false,
      done = false;
    const logs: string[] = [];
    let timeout: ReturnType<typeof setTimeout>;
    const finish = (
      type: 'complete' | 'error',
      value: {
        tick?: unknown;
        result?: unknown;
        text?: unknown;
        diagnostic?: unknown;
      },
    ) => {
      if (done) return;
      done = true;
      clearTimeout(timeout);
      const text = JSON.stringify({
        revision: run.revision,
        seed: run.seed,
        opponent: run.opponent,
        tick: value.tick,
        result: value.result ?? value.text,
        diagnostic: value.diagnostic,
        logs,
      }).slice(0, 16000);
      setStatus(
        type === 'error'
          ? 'Playtest failed: ' + String(value.text ?? value.result).slice(0, 2000)
          : `Finished: ${String(value.result).slice(0, 2000)}`,
      );
      callback.current(text);
    };
    const waitForLaunch = () => {
      clearTimeout(timeout);
      timeout = setTimeout(
        () =>
          finish('error', {
            text: 'The game did not start in time. Check browser support or restart the playtest.',
          }),
        90000,
      );
    };
    waitForLaunch();
    const listener = async (event: MessageEvent) => {
      const m = event.data;
      if (
        event.origin !== location.origin ||
        event.source !== frame.current?.contentWindow ||
        !m ||
        m.channel !== 'glob2-ai-studio' ||
        m.version !== 1 ||
        m.runId !== run.runId ||
        m.revision !== run.revision
      )
        return;
      if (m.type === 'ready' && !launching) {
        // The runtime may reload itself to fall back from threads to serial mode.
        launching = true;
        done = false;
        setStatus('Loading game…');
        waitForLaunch();
        try {
          const response = await fetch('/api/v1/ai-studio/test-map', {
            credentials: 'same-origin',
            signal: abort.signal,
          });
          if (!response.ok) throw Error('Could not load test map.');
          const map = await response.arrayBuffer();
          if (abort.signal.aborted) return;
          frame.current?.contentWindow?.postMessage(
            { channel: 'glob2-ai-studio', version: 1, type: 'launch', ...run, map },
            location.origin,
            [map],
          );
        } catch (e) {
          if (!abort.signal.aborted) finish('error', { text: String(e).slice(0, 2000) });
        } finally {
          launching = false;
        }
      }
      if (m.type === 'progress' && !done) {
        clearTimeout(timeout);
        setStatus(`Revision ${run.revision} · tick ${Number(m.tick) || 0} · live`);
      }
      if (m.type === 'diagnostic' && typeof m.text === 'string' && logs.length < 40) {
        logs.push(m.text.slice(0, 2000));
        setDiagnostics([...logs]);
      }
      if (m.type === 'complete' || m.type === 'error') finish(m.type, m);
    };
    window.addEventListener('message', listener);
    return () => {
      abort.abort();
      clearTimeout(timeout);
      window.removeEventListener('message', listener);
      contentWindow?.postMessage(
        {
          channel: 'glob2-ai-studio',
          version: 1,
          type: 'stop',
          runId: run.runId,
          revision: run.revision,
        },
        location.origin,
      );
    };
  }, [run]);
  return (
    <div className="as-playtest">
      <p role="status">{status}</p>
      <iframe
        ref={frame}
        title={`Live AI playtest revision ${run.revision}`}
        src={`/play/studio.html?run=${run.runId}&revision=${run.revision}`}
        allow="cross-origin-isolated; fullscreen"
      />
      <details>
        <summary>Runtime diagnostics ({diagnostics.length})</summary>
        <pre>{diagnostics.join('\n')}</pre>
      </details>
    </div>
  );
}
