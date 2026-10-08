import { useEffect, useRef, useState } from 'react';
import type { SetPackage } from '@glob2/protocol';
/** The browser build runs the same bounded import and renderer as native validation. */
export function SetPreview({ pack }: { pack: SetPackage }) {
  const frame = useRef<HTMLIFrameElement>(null);
  const [run, setRun] = useState<{ id: string; source: string } | null>(null),
    [image, setImage] = useState(''),
    [status, setStatus] = useState('');
  useEffect(() => {
    if (!run) return;
    const timer = setTimeout(
      () => setStatus('The preview engine did not finish. Check the game runtime and try again.'),
      90000,
    );
    let url = '';
    const receive = (event: MessageEvent) => {
      const m = event.data;
      if (
        event.origin !== location.origin ||
        event.source !== frame.current?.contentWindow ||
        m?.channel !== 'glob2-set-preview' ||
        m.version !== 1 ||
        m.runId !== run.id ||
        m.revision !== 1
      )
        return;
      if (m.type === 'ready')
        frame.current?.contentWindow?.postMessage(
          {
            channel: 'glob2-set-preview',
            version: 1,
            type: 'launch',
            runId: run.id,
            revision: 1,
            source: run.source,
          },
          location.origin,
        );
      if (m.type === 'error') {
        clearTimeout(timer);
        setStatus(String(m.text).slice(0, 2000));
      }
      if (m.type === 'complete') {
        clearTimeout(timer);
        if (m.report?.valid && m.png instanceof Uint8Array && m.png.byteLength <= 4 * 1024 * 1024) {
          url = URL.createObjectURL(new Blob([new Uint8Array(m.png)], { type: 'image/png' }));
          setImage(url);
          setStatus(
            'Rendered by the browser game engine. Run publishing checks to validate the saved revision.',
          );
        } else setStatus(String(m.report?.reason ?? 'Preview failed.').slice(0, 2000));
      }
    };
    window.addEventListener('message', receive);
    return () => {
      clearTimeout(timer);
      window.removeEventListener('message', receive);
      if (url) URL.revokeObjectURL(url);
    };
  }, [run]);
  return (
    <div>
      <h3>Game preview</h3>
      <button
        type="button"
        onClick={() => {
          setStatus('Loading browser renderer…');
          setImage('');
          setRun({ id: crypto.randomUUID(), source: JSON.stringify(pack) });
        }}
      >
        Preview current changes
      </button>
      <p role="status">{status}</p>
      {run && (
        <iframe
          key={run.id}
          ref={frame}
          title="Set preview engine"
          tabIndex={-1}
          aria-hidden="true"
          src={`/play/set-preview.html?threads=serial&renderer=software&run=${run.id}&revision=1`}
          style={{ width: 1, height: 1, border: 0 }}
        />
      )}
      {image && (
        <img
          className="set-contact"
          src={image}
          alt="Current custom terrain and resource sprites rendered by the game"
        />
      )}
    </div>
  );
}
