import { build } from 'esbuild';
import { readFile, mkdir, copyFile, writeFile, access } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { locales } from '../src/locales.ts';
const root = fileURLToPath(new URL('../../../../', import.meta.url));
const browser = `${root}/browser`;
const output = await build({
  stdin: {
    contents: `import * as i18n from './platform/packages/i18n/src/index.ts';
      const base = new URL('.', document.currentScript.src);
      i18n.configureCatalogLoader(async locale => {
        const response = await fetch(new URL('locales/' + locale + '.json', base));
        if (!response.ok) throw new Error('Language download failed');
        return response.json();
      });
      globalThis.Glob2I18n = { ...i18n, get locale() { return i18n.getLocale(); } };`,
    resolveDir: root,
    loader: 'ts',
  },
  bundle: true,
  format: 'iife',
  target: 'es2022',
  minify: true,
  write: false,
  plugins: [
    {
      name: 'external-catalog-fetch',
      setup(builder) {
        builder.onLoad({ filter: /i18n\/src\/index\.ts$/ }, async (args) => ({
          contents: (await readFile(args.path, 'utf8')).replace(
            /const loaders: Record<string, \(\) => Promise<\{ default: Catalog \}>> = \{[\s\S]*?\n\};/,
            'const loaders: Record<string, () => Promise<{ default: Catalog }>> = {};',
          ),
          loader: 'ts',
        }));
      },
    },
  ],
});
const generated =
  '// Generated from @glob2/i18n by npm run i18n:browser; do not edit.\n' +
  output.outputFiles[0]!.text;
if (process.argv.includes('--check')) {
  if ((await readFile(`${browser}/i18n.js`, 'utf8')) !== generated)
    throw new Error('Browser localization runtime is stale: npm run i18n:browser');
} else await writeFile(`${browser}/i18n.js`, generated);
if (process.argv.includes('--check')) {
  // Catalog copies are development build outputs, not required repository
  // fixtures. Verify any existing copies without creating or rewriting them.
  for (const locale of locales) {
    const path = `${browser}/locales/${locale.code}.json`;
    try {
      await access(path);
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === 'ENOENT') continue;
      throw error;
    }
    const canonical = await readFile(new URL(`../locales/${locale.code}.json`, import.meta.url));
    if (!(await readFile(path)).equals(canonical))
      throw new Error(`Browser ${locale.code} catalog is stale: npm run i18n:browser`);
  }
} else {
  await mkdir(`${browser}/locales`, { recursive: true });
  for (const locale of locales)
    await copyFile(
      new URL(`../locales/${locale.code}.json`, import.meta.url),
      `${browser}/locales/${locale.code}.json`,
    );
}
console.log(`Built launcher runtime and ${locales.length} catalogs`);
