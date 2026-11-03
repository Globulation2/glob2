// Build bootstrap shared in behavior by the online app and static website.
import { execFileSync } from 'node:child_process';
import {
  mkdirSync,
  readFileSync,
  writeFileSync,
  copyFileSync,
  existsSync,
  appendFileSync,
  readdirSync,
  cpSync,
  rmSync,
} from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, dirname } from 'node:path';
import { createHash } from 'node:crypto';
function runNpm(args, options) {
  const cli =
    process.env.npm_execpath ?? join(dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js');
  if (existsSync(cli)) return execFileSync(process.execPath, [cli, ...args], options);
  if (process.platform === 'win32')
    throw new Error('Run this build through npm so npm_execpath identifies its CLI');
  return execFileSync('npm', args, options);
}
const repository = 'https://github.com/Globulation2/glob2-design-system.git';
const root = fileURLToPath(new URL('../', import.meta.url));
export function resolveDesignSystem() {
  const requested = process.env.GLOB2_DESIGN_SYSTEM_SHA;
  if (requested && !/^[a-f0-9]{40}$/.test(requested))
    throw new Error('GLOB2_DESIGN_SYSTEM_SHA must be a full commit SHA');
  const sha =
    requested ??
    execFileSync('git', ['ls-remote', repository, 'refs/heads/main'], { encoding: 'utf8' }).split(
      /\s/,
    )[0];
  if (!/^[a-f0-9]{40}$/.test(sha)) throw new Error('Could not resolve design-system main');
  return sha;
}
export function syncDesignSystem() {
  const sha = resolveDesignSystem();
  const metaPath = join(root, '.cache/design-system.json');
  const install = join(root, 'node_modules/@glob2/design-system');
  let previous;
  try {
    previous = JSON.parse(readFileSync(metaPath, 'utf8'));
  } catch {
    /* first build */
  }
  // A fresh npm ci can restore the lockfile version. Hash the installed outputs too.
  const fingerprint = (directory) => {
    const hash = createHash('sha256');
    const walk = (path) => {
      for (const entry of readdirSync(path, { withFileTypes: true }).sort((a, b) =>
        a.name.localeCompare(b.name),
      )) {
        const file = join(path, entry.name);
        hash.update(file.slice(directory.length));
        if (entry.isDirectory()) walk(file);
        else hash.update(readFileSync(file));
      }
    };
    walk(directory);
    return hash.digest('hex');
  };
  if (
    previous?.sha !== sha ||
    !existsSync(join(install, 'dist/bootstrap.js')) ||
    previous.installHash !== fingerprint(install)
  ) {
    // Install in isolation so resolving the theme cannot re-resolve other dependencies
    // or omit tools when Vite/Astro sets NODE_ENV=production while loading config.
    const cache = join(root, '.cache/design-system-install');
    rmSync(cache, { recursive: true, force: true });
    mkdirSync(cache, { recursive: true });
    runNpm(
      [
        'install',
        '--prefix',
        cache,
        '--omit=dev',
        '--no-save',
        '--package-lock=false',
        '--ignore-scripts',
        '--no-audit',
        '--no-fund',
        `@glob2/design-system@git+${repository}#${sha}`,
      ],
      { cwd: cache, stdio: 'inherit', env: { ...process.env, NODE_ENV: 'production' } },
    );
    const candidate = join(cache, 'node_modules/@glob2/design-system');
    const manifest = JSON.parse(readFileSync(join(candidate, 'package.json'), 'utf8'));
    if (Object.keys(manifest.dependencies ?? {}).length)
      throw new Error(
        'Design-system runtime dependencies require an explicit consumer dependency update',
      );
    rmSync(install, { recursive: true, force: true });
    cpSync(candidate, install, { recursive: true });
    rmSync(cache, { recursive: true, force: true });
  }
  mkdirSync(join(root, '.cache'), { recursive: true });
  const metadata = { repository, sha, installHash: fingerprint(install) };
  writeFileSync(metaPath, JSON.stringify(metadata, null, 2) + '\n');
  if (process.env.GITHUB_ENV)
    appendFileSync(process.env.GITHUB_ENV, `GLOB2_DESIGN_SYSTEM_SHA=${sha}\n`);
  console.log(`Glob2 design system: ${sha}`);
  return metadata;
}
export function stageDesignAssets(publicDir, metadata, brand = true) {
  const install = join(root, 'node_modules/@glob2/design-system');
  rmSync(join(publicDir, 'brand'), { recursive: true, force: true });
  mkdirSync(join(publicDir, 'brand'), { recursive: true });
  const files = [
    'glob2-sans.woff2',
    'nunito.woff2',
    'glob-64.png',
    'wordmark-letters.webp',
    'wordmark-two.webp',
    'colony-960.webp',
    'colony-1600.webp',
    'colony-loop.mp4',
    'swarm.webp',
    'wood.webp',
    'fruit.webp',
    'war-flag.webp',
    'exploration-flag.webp',
  ];
  if (brand)
    for (const file of files)
      copyFileSync(join(install, 'assets', file), join(publicDir, 'brand', file));
  copyFileSync(join(install, 'dist/bootstrap.js'), join(publicDir, 'theme.js'));
  for (const file of ['favicon-32.png', 'apple-touch-icon.png'])
    copyFileSync(join(install, 'assets', file), join(publicDir, file));
  // The /fonts alias is retained for the online app's preload and existing links.
  mkdirSync(join(publicDir, 'fonts'), { recursive: true });
  copyFileSync(join(install, 'assets/glob2-sans.woff2'), join(publicDir, 'fonts/glob2-sans.woff2'));
  for (const file of ['LICENSE-DejaVu.txt', 'LICENSE-Nunito.txt', 'LICENSE-Tabler.txt'])
    copyFileSync(join(install, 'assets', file), join(publicDir, 'fonts', file));
  writeFileSync(
    join(publicDir, 'design-system.json'),
    JSON.stringify({ repository: metadata.repository, sha: metadata.sha }) + '\n',
  );
}
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  if (process.argv.includes('--resolve-only')) console.log(resolveDesignSystem());
  else syncDesignSystem();
}
