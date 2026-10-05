import { readFile, mkdtemp, rm, mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { it, expect } from 'vitest';
import { createRunner, requireBoundedMemory } from '../src/runner.ts';
// Opt-in integration test: preprovision assets, dependencies and a bounded tmpfs.
// MUSIC_SANDBOX_TEST=1 MUSIC_PYTHON=... MUSIC_ASSETS=... npm test -- runner.test.ts
it.skipIf(process.env['MUSIC_SANDBOX_TEST'] !== '1')(
  'isolates generated code and independently rejects forged data',
  async () => {
    const scratch = await mkdtemp((process.env['MUSIC_SCRATCH'] ?? '/dev/shm') + '/glob2-music-');
    try {
      const runner = await createRunner(
        resolve('../tools/music'),
        resolve(process.env['MUSIC_ASSETS'] ?? '../tools/music/cache'),
        scratch,
        resolve(process.env['MUSIC_PYTHON'] ?? '../tools/music/.venv/bin/python'),
      );
      const source = await readFile(
        resolve('../tools/music/sets/moss-lanterns/composition.py'),
        'utf8',
      );
      const metadata = {
        id: 'sandbox',
        origin: 'https://music.invalid',
        title: 'Sandbox',
        artist: 'Test',
        description: '',
        license: 'CC0-1.0' as const,
        credits: '',
        sources: [],
        tags: [],
        aiGenerated: true,
      };
      const run = (code: string, signal = AbortSignal.timeout(30000)) =>
        runner.run(code, { pipeline: 'acoustic-v1', seed: 0 }, false, signal, () => {}, metadata);
      const probes = `
import os, socket
assert 'MUSIC_OPENAI_API_KEY' not in os.environ
assert 'DATABASE_URL' not in os.environ
assert not os.path.exists('/home/bradley/.ssh')
try:
    open('/pipeline/tools/music/escape', 'w').write('bad')
    raise AssertionError('Dependencies were writable')
except OSError: pass
try:
    open('/escape','w').write('bad')
    raise AssertionError('Root filesystem was writable')
except OSError: pass
try:
    socket.create_connection(('1.1.1.1',443),timeout=.1)
    raise AssertionError('Network was available')
except OSError: pass
import subprocess
children=[]
try:
    try:
        for _ in range(80): children.append(subprocess.Popen(['/usr/bin/sleep','20']))
    except OSError: pass
    assert len(children) < 64, 'Process limit was not enforced'
finally:
    for child in children: child.terminate()
    for child in children: child.wait()
`;
      expect((await run(source + probes)).report.checks[0]?.name).toBe('score');
      await expect(run(source + '\nSCORE.bars = 999999\n')).rejects.toThrow();
      await expect(
        run(
          "import os\nos.symlink('/pipeline/tools/music/requirements.txt','/job/score.json')\nos._exit(0)\n",
        ),
      ).rejects.toThrow(/Invalid exported score/);
      await expect(
        run(
          "from pathlib import Path\nPath('/job/result.json').write_text('{\"passed\":true}')\nraise RuntimeError('forged report')\n",
        ),
      ).rejects.toThrow();
      await expect(run('huge=bytearray(12*1024**3)')).rejects.toThrow();
      await expect(run('while True: pass', AbortSignal.timeout(1200))).rejects.toThrow();
      await expect(run("open('/job/huge','wb').write(b'x'*300000000)")).rejects.toThrow();
    } finally {
      await rm(scratch, { recursive: true, force: true });
    }
  },
  120000,
);

it.skipIf(process.env['MUSIC_RENDER_TEST'] !== '1')(
  'renders both CPU palettes through independent encoded QA',
  async () => {
    const scratch = await mkdtemp((process.env['MUSIC_SCRATCH'] ?? '/dev/shm') + '/glob2-music-');
    try {
      const runner = await createRunner(
        resolve('../tools/music'),
        resolve(process.env['MUSIC_ASSETS'] ?? '../tools/music/cache'),
        scratch,
        resolve(process.env['MUSIC_PYTHON'] ?? '../tools/music/.venv/bin/python'),
      );
      for (const [pipeline, set] of [
        ['acoustic-v1', 'moss-lanterns'],
        ['synth-v1', 'glass-garden'],
      ] as const) {
        const start = Date.now();
        const source = await readFile(
          resolve('../tools/music/sets', set, 'composition.py'),
          'utf8',
        );
        const result = await runner.run(
          source,
          { pipeline, seed: 0 },
          true,
          AbortSignal.timeout(1200000),
          () => {},
          {
            id: set,
            origin: 'https://music.invalid',
            title: set,
            artist: 'Test',
            description: 'CPU baseline',
            license: 'CC0-1.0',
            credits: 'Local baseline',
            sources: [],
            tags: [],
            aiGenerated: true,
          },
        );
        const directory = resolve('../artifacts/music-studio-renders', pipeline);
        await mkdir(directory, { recursive: true });
        await writeFile(
          resolve(directory, 'report.json'),
          JSON.stringify(
            { ...result.report, elapsedSeconds: (Date.now() - start) / 1000 },
            null,
            2,
          ),
        );
        for (const [name, bytes] of Object.entries(result.files))
          await writeFile(resolve(directory, name), bytes);
        expect(result.report.checks.map((c) => c.name)).toEqual([
          'score',
          'format',
          'loudness',
          'seam',
          'repetition',
          'alignment',
          'contrast',
          'noise',
          'balance',
          'audibility',
          'dropout',
        ]);
        expect(result.report.result?.frames).toBeGreaterThanOrEqual(2400000);
        expect(result.files['set.zip']?.length).toBeGreaterThan(1000);
      }
    } finally {
      await rm(scratch, { recursive: true, force: true });
    }
  },
  2500000,
);

it('requires a finite aggregate memory limit on the worker cgroup or an ancestor', async () => {
  const directory = await mkdtemp('/tmp/music-cgroup-');
  try {
    await mkdir(resolve(directory, 'parent/worker'), { recursive: true });
    const membership = resolve(directory, 'membership');
    await writeFile(membership, '0::/parent/worker\n');
    await writeFile(resolve(directory, 'parent/worker/memory.max'), 'max\n');
    await expect(requireBoundedMemory(directory, membership)).rejects.toThrow(/12 GiB/);
    await writeFile(resolve(directory, 'parent/memory.max'), String(12 * 1024 ** 3));
    await expect(requireBoundedMemory(directory, membership)).resolves.toBeUndefined();
    await writeFile(resolve(directory, 'parent/memory.max'), String(16 * 1024 ** 3));
    await expect(requireBoundedMemory(directory, membership)).rejects.toThrow(/12 GiB/);
    await writeFile(membership, '0::/../../elsewhere\n');
    await expect(requireBoundedMemory(directory, membership)).rejects.toThrow(/cgroup v2/);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
