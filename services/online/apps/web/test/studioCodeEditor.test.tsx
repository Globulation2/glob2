// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, render } from '@testing-library/react';
const { active, paths } = vi.hoisted(() => ({ active: new Set<string>(), paths: [] as string[] }));
vi.mock('monaco-editor', () => {
  const createModel = (source: string, _language: string, path?: string) => {
    if (path) paths.push(path);
    return {
      getValue: () => source,
      getFullModelRange: () => ({}),
      onDidChangeContent: () => ({ dispose() {} }),
      dispose() {},
    };
  };
  const editor = () => ({ dispose() {}, updateOptions() {}, pushUndoStop() {}, executeEdits() {} });
  return {
    Uri: { parse: (path: string) => path },
    typescript: {
      ScriptTarget: { ES2020: 2020 },
      javascriptDefaults: {
        setCompilerOptions() {},
        addExtraLib(_source: string, path: string) {
          active.add(path);
          return {
            dispose() {
              active.delete(path);
            },
          };
        },
      },
    },
    editor: {
      setTheme() {},
      createModel,
      create: editor,
      createDiffEditor: () => {
        let original: ReturnType<typeof createModel>;
        return {
          ...editor(),
          setModel(models: { original: ReturnType<typeof createModel> }) {
            original = models.original;
          },
          getOriginalEditor: () => ({ getValue: original.getValue, setValue() {} }),
        };
      },
    },
  };
});
import CodeEditor from '../src/components/studio/CodeEditor.tsx';
afterEach(() => {
  cleanup();
  active.clear();
  paths.length = 0;
});
const generator = [
  { path: 'file:///generator-studio/toolkit.d.ts', source: 'export interface GeneratorContext{}' },
];
const ai = [{ path: 'file:///studio/glob2.d.ts', source: 'declare function makeAI():void;' }];
it('keeps declarations while a diff shares them and releases them when changing domains', () => {
  const main = render(
    <CodeEditor
      source="generator"
      modelPath="file:///generator-studio/map.js"
      declarations={generator}
      readOnly={false}
      onChange={() => {}}
    />,
  );
  const diff = render(
    <CodeEditor
      source="generator"
      baseline="old"
      modelPath="file:///generator-studio/map.js"
      declarations={generator}
      readOnly
      onChange={() => {}}
    />,
  );
  expect(active.has(generator[0]?.path ?? '')).toBe(true);
  diff.unmount();
  expect(active.has(generator[0]?.path ?? '')).toBe(true);
  main.unmount();
  expect(active.size).toBe(0);
  render(<CodeEditor source="AI" declarations={ai} readOnly={false} onChange={() => {}} />);
  expect([...active]).toEqual([ai[0]?.path]);
  expect(paths).toContain('file:///generator-studio/map.js.diff');
  expect(paths).toContain('file:///studio/ai.js');
});
