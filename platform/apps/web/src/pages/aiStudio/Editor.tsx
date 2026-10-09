import CodeEditor from '../../components/studio/CodeEditor.tsx';
import api1 from '../../../../../../examples/javascript/glob2.d.ts?raw';
import api2 from '../../../../../../examples/javascript/glob2-v2.d.ts?raw';
const declarations = [
  { path: 'file:///studio/glob2.d.ts', source: api1 },
  { path: 'file:///studio/glob2-v2.d.ts', source: api2 },
];
export default function Editor(props: Parameters<typeof CodeEditor>[0]) {
  return <CodeEditor {...props} declarations={declarations} />;
}
