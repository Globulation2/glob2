import { lazy, Suspense, useMemo, useState } from 'react';
import { decodeGeneratorDraft, encodeGeneratorDraft } from '@glob2/protocol';
import toolkit from '../../../../../../data/generators/toolkit.d.ts?raw';
const CodeEditor = lazy(() => import('../../components/studio/CodeEditor.tsx'));
export default function Editor({
  source,
  baseline,
  readOnly,
  onChange,
  small = false,
}: {
  source: string;
  baseline?: string;
  readOnly: boolean;
  onChange: (source: string) => void;
  small?: boolean;
}) {
  const [file, setFile] = useState<'script' | 'manifest'>('script');
  const d = decodeGeneratorDraft(source),
    b = baseline ? decodeGeneratorDraft(baseline) : undefined;
  let error = '';
  let entry = 'generator.js';
  try {
    const manifest = JSON.parse(d.manifest) as { entry?: unknown };
    if (
      typeof manifest?.entry === 'string' &&
      /^[a-zA-Z0-9_./-]+\.js$/.test(manifest.entry) &&
      !manifest.entry.split('/').includes('..')
    )
      entry = manifest.entry;
  } catch (e) {
    error = e instanceof Error ? e.message : 'Invalid manifest JSON.';
  }
  const toolkitPath = `file:///generator-studio/${entry.slice(0, entry.lastIndexOf('/') + 1)}toolkit.d.ts`;
  const declarations = useMemo(() => [{ path: toolkitPath, source: toolkit }], [toolkitPath]);
  const label = file === 'script' ? 'Generator JavaScript source' : 'Generator manifest JSON';
  const change = (text: string) => onChange(encodeGeneratorDraft({ ...d, [file]: text }));
  return (
    <>
      <div className="as-toolbar">
        <label>
          File{' '}
          <select value={file} onChange={(e) => setFile(e.target.value as typeof file)}>
            <option value="script">JavaScript · {entry}</option>
            <option value="manifest">Manifest</option>
          </select>
        </label>
      </div>
      {error && <p role="status">Manifest JSON: {error}</p>}
      {small ? (
        <>
          <textarea
            className="as-source"
            aria-label={label}
            spellCheck={false}
            readOnly={readOnly}
            value={d[file]}
            onChange={(e) => change(e.target.value)}
          />
          {b && (
            <details>
              <summary>Compared revision</summary>
              <pre>{b[file]}</pre>
            </details>
          )}
        </>
      ) : (
        <Suspense fallback={<p>Loading code editor…</p>}>
          <CodeEditor
            key={file}
            source={d[file]}
            baseline={baseline === undefined ? undefined : (b?.[file] ?? '')}
            readOnly={readOnly}
            onChange={change}
            language={file === 'manifest' ? 'json' : 'javascript'}
            modelPath={`file:///generator-studio/${file === 'manifest' ? 'manifest.json' : entry}`}
            label={label}
            declarations={declarations}
          />
        </Suspense>
      )}
    </>
  );
}
