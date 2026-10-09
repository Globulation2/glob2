import {
  mkdtempSync,
  mkdirSync,
  copyFileSync,
  readFileSync,
  writeFileSync,
  symlinkSync,
  rmSync,
  existsSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { it, expect } from 'vitest';
import { transformSync } from 'esbuild';
import { locales } from '../src/locales.ts';

it('browser check rejects stale copies without rewriting or creating development outputs', () => {
  const packageRoot = fileURLToPath(new URL('../', import.meta.url));
  const fixture = mkdtempSync(join(tmpdir(), 'glob2-i18n-build-'));
  try {
    const pkg = join(fixture, 'platform/packages/i18n');
    mkdirSync(join(pkg, 'scripts'), { recursive: true });
    mkdirSync(join(pkg, 'src'), { recursive: true });
    mkdirSync(join(pkg, 'locales'), { recursive: true });
    mkdirSync(join(fixture, 'browser'));
    symlinkSync(
      join(packageRoot, '../../node_modules'),
      join(fixture, 'platform/node_modules'),
      'dir',
    );
    for (const file of ['scripts/build-browser.ts', 'src/index.ts', 'src/locales.ts']) {
      copyFileSync(join(packageRoot, file), join(pkg, file));
    }
    for (const locale of locales) {
      writeFileSync(
        join(pkg, `locales/${locale.code}.json`),
        JSON.stringify({ Language: locale.code }),
      );
    }
    writeFileSync(
      join(pkg, 'src/locales.js'),
      transformSync(readFileSync(join(pkg, 'src/locales.ts'), 'utf8'), {
        loader: 'ts',
        format: 'esm',
      }).code,
    );
    const script = join(pkg, 'scripts/build-browser.mjs');
    writeFileSync(
      script,
      transformSync(
        readFileSync(join(pkg, 'scripts/build-browser.ts'), 'utf8').replace(
          "'../src/locales.ts'",
          "'../src/locales.js'",
        ),
        { loader: 'ts', format: 'esm' },
      ).code,
    );
    const run = (...args: string[]) =>
      spawnSync(process.execPath, [script, ...args], { encoding: 'utf8', cwd: fixture });
    const built = run();
    expect(built.status, built.stderr).toBe(0);
    const path = join(fixture, 'browser/locales/fr.json');
    const stale = '{"Language":"stale fixture"}';
    writeFileSync(path, stale);
    const failed = run('--check');
    expect(failed.status).not.toBe(0);
    expect(failed.stderr).toContain('fr catalog is stale');
    expect(readFileSync(path, 'utf8')).toBe(stale);
    copyFileSync(join(pkg, 'locales/fr.json'), path);
    rmSync(join(fixture, 'browser/locales'), { recursive: true });
    const absent = run('--check');
    expect(absent.status, absent.stderr).toBe(0);
    expect(existsSync(dirname(path))).toBe(false);
  } finally {
    rmSync(fixture, { recursive: true, force: true });
  }
});
