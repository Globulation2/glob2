import { t } from '../../i18n.tsx';
import { useLocale } from '../../i18n.tsx';
import { useTheme } from '../../theme.tsx';
import { useEffect, useRef } from 'react';
import * as monaco from 'monaco-editor';
import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';
import JsonWorker from 'monaco-editor/esm/vs/language/json/json.worker?worker';
import TsWorker from 'monaco-editor/esm/vs/language/typescript/ts.worker?worker';
(globalThis as typeof globalThis & { MonacoEnvironment: unknown }).MonacoEnvironment = {
  getWorker: (_id: string, label: string) =>
    label === 'json'
      ? new JsonWorker()
      : label === 'javascript' || label === 'typescript'
        ? new TsWorker()
        : new EditorWorker(),
};
monaco.typescript.javascriptDefaults.setCompilerOptions({
  allowJs: true,
  checkJs: true,
  target: monaco.typescript.ScriptTarget.ES2020,
  noLib: false,
  allowNonTsExtensions: true,
});
const declarationLeases = new Map<string, { references: number; disposable: monaco.IDisposable }>();
const noDeclarations: readonly { path: string; source: string }[] = [];
function acquireDeclaration(d: { path: string; source: string }) {
  const lease = declarationLeases.get(d.path) ?? {
    references: 0,
    disposable: monaco.typescript.javascriptDefaults.addExtraLib(d.source, d.path),
  };
  lease.references++;
  declarationLeases.set(d.path, lease);
  return {
    dispose() {
      if (--lease.references === 0) {
        lease.disposable.dispose();
        declarationLeases.delete(d.path);
      }
    },
  };
}
export default function Editor({
  source,
  baseline,
  language = 'javascript',
  modelPath = 'file:///studio/ai.js',
  label = t('AI JavaScript source'),
  declarations = noDeclarations,
  readOnly,
  onChange,
}: {
  source: string;
  language?: string;
  modelPath?: string;
  label?: string;
  declarations?: readonly { path: string; source: string }[];
  baseline?: string;
  readOnly: boolean;
  onChange: (text: string) => void;
}) {
  useLocale();
  const { theme } = useTheme();
  useEffect(() => {
    monaco.editor.setTheme(theme === 'dark' ? 'vs-dark' : 'vs');
  }, [theme]);
  const host = useRef<HTMLDivElement>(null),
    editor = useRef<monaco.editor.IStandaloneCodeEditor | monaco.editor.IStandaloneDiffEditor>(
      null,
    ),
    model = useRef<monaco.editor.ITextModel>(null);
  const change = useRef(onChange);
  useEffect(() => {
    change.current = onChange;
  }, [onChange]);
  const initial = useRef({ source, baseline, readOnly, theme });
  const diff = baseline !== undefined;
  useEffect(() => {
    if (!host.current) return;
    const libs = declarations.map(acquireDeclaration);
    const m = monaco.editor.createModel(
      initial.current.source,
      language,
      monaco.Uri.parse(modelPath + (diff ? '.diff' : '')),
    );
    model.current = m;
    const options = {
      automaticLayout: true,
      minimap: { enabled: false },
      fontSize: 14,
      readOnly: initial.current.readOnly,
      scrollBeyondLastLine: false,
      theme: initial.current.theme === 'dark' ? 'vs-dark' : 'vs',
      ariaLabel: label,
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
      original = monaco.editor.createModel(initial.current.baseline ?? '', language);
      (e as monaco.editor.IStandaloneDiffEditor).setModel({ original, modified: m });
    }
    const listener = m.onDidChangeContent(() => change.current(m.getValue()));
    return () => {
      listener.dispose();
      for (const lib of libs) lib.dispose();
      e.dispose();
      m.dispose();
      original?.dispose();
      editor.current = null;
      model.current = null;
    };
  }, [diff, language, modelPath, label, declarations]);
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
