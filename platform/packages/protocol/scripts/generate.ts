// Regenerates packages/protocol/fixtures/ from the schemas and fixtureCases.ts.
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildFixtureFiles } from './fixtureFiles.ts';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures');
const files = buildFixtureFiles();
rmSync(root, { recursive: true, force: true });
for (const [path, content] of files) {
  const target = join(root, path);
  mkdirSync(dirname(target), { recursive: true });
  writeFileSync(target, content);
}
console.log(`wrote ${files.size} files to ${root}`);
