import { useEffect, useRef } from 'react';
import * as monaco from 'monaco-editor';
import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';
import TsWorker from 'monaco-editor/esm/vs/language/typescript/ts.worker?worker';
import api1 from '../../../../../../examples/javascript/glob2.d.ts?raw';
import api2 from '../../../../../../examples/javascript/glob2-v2.d.ts?raw';
(globalThis as typeof globalThis & { MonacoEnvironment: unknown }).MonacoEnvironment = {
  getWorker: (_id: string, label: string) =>
    label === 'javascript' || label === 'typescript' ? new TsWorker() : new EditorWorker(),
};
monaco.typescript.javascriptDefaults.setCompilerOptions({
  allowJs: true,
  checkJs: true,
  target: monaco.typescript.ScriptTarget.ES2020,
  noLib: false,
  allowNonTsExtensions: true,
});
monaco.typescript.javascriptDefaults.addExtraLib(api1, 'file:///studio/glob2.d.ts');
monaco.typescript.javascriptDefaults.addExtraLib(api2, 'file:///studio/glob2-v2.d.ts');
export default function Editor({
  source,
  baseline,
  readOnly,
  onChange,
}: {
  source: string;
  baseline?: string;
  readOnly: boolean;
  onChange: (text: string) => void;
}) {
  const host = useRef<HTMLDivElement>(null),
    editor = useRef<monaco.editor.IStandaloneCodeEditor | monaco.editor.IStandaloneDiffEditor>(
      null,
    ),
    model = useRef<monaco.editor.ITextModel>(null);
  const change = useRef(onChange);
  useEffect(() => {
    change.current = onChange;
  }, [onChange]);
  const initial = useRef({ source, baseline, readOnly });
  const diff = baseline !== undefined;
  useEffect(() => {
    if (!host.current) return;
    const m = monaco.editor.createModel(
      initial.current.source,
      'javascript',
      diff ? undefined : monaco.Uri.parse('file:///studio/ai.js'),
    );
    model.current = m;
    const options = {
      automaticLayout: true,
      minimap: { enabled: false },
      fontSize: 14,
      readOnly: initial.current.readOnly,
      scrollBeyondLastLine: false,
      theme: 'vs-dark',
      ariaLabel: 'AI JavaScript source',
    };
    const e = diff
      ? monaco.editor.createDiffEditor(host.current, {
          ...options,
          readOnly: true,
          renderSideBySide: false,
        })
      : monaco.editor.create(host.current, { ...options, model: m });
    editor.current = e;
    let original: monaco.editor.ITextModel | undefined;
    if (diff) {
      original = monaco.editor.createModel(initial.current.baseline ?? '', 'javascript');
      (e as monaco.editor.IStandaloneDiffEditor).setModel({ original, modified: m });
    }
    const listener = m.onDidChangeContent(() => change.current(m.getValue()));
    return () => {
      listener.dispose();
      e.dispose();
      m.dispose();
      original?.dispose();
      editor.current = null;
      model.current = null;
    };
  }, [diff]);
  useEffect(() => {
    const m = model.current,
      e = editor.current;
    if (!m || !e) return;
    if (m.getValue() !== source) {
      const code = 'getModifiedEditor' in e ? e.getModifiedEditor() : e;
      code.pushUndoStop();
      code.executeEdits('assistant', [{ range: m.getFullModelRange(), text: source }]);
      code.pushUndoStop();
    }
    e.updateOptions({ readOnly: readOnly || diff });
    if ('getOriginalEditor' in e && e.getOriginalEditor().getValue() !== baseline)
      e.getOriginalEditor().setValue(baseline ?? '');
  }, [source, baseline, readOnly, diff]);
  return <div className="as-editor" ref={host} />;
}
