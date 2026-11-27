import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import type { CrashFrame } from '@glob2/protocol';
import { lookup } from '../src/breakpad.ts';
import { machoDebugId, parseMetricKit, symbolicateMetricKit } from '../src/metrickit.ts';
import { framesOf } from '../src/minidump.ts';
import { compareVersions, inGame, normalizeFunction, signature } from '../src/signature.ts';
import { breakpadPath, safeModule, type SymbolCache } from '../src/symbols.ts';
import { symbolicateWeb, wasmFrames } from '../src/web.ts';
import { javaFrames, symbolicateJava } from '../src/java.ts';
import { errorSignature, readErrorReport } from '../src/error.ts';
import { UnreadableCrash } from '../src/minidump.ts';

const work = mkdtempSync(join(tmpdir(), 'glob2-crash-test-'));

/** A cache that serves fixed files and records what was asked for. */
function cacheOf(files: Record<string, string>): SymbolCache & { asked: string[] } {
  const asked: string[] = [];
  return {
    root: work,
    asked,
    file: async (kind: string, module: string, debugId: string) => {
      asked.push(`${kind}:${module}:${debugId}`);
      return files[`${kind}:${module}:${debugId}`];
    },
    evict: async () => undefined,
  } as unknown as SymbolCache & { asked: string[] };
}

const SYM = [
  'MODULE mac arm64 70B89F2716343580A69557CDB41D77430 glob2',
  'FILE 0 /home/runner/work/glob2/glob2/src/unit/Unit.cpp',
  'FILE 1 /home/runner/work/glob2/glob2/third_party/sdl/src/SDL.c',
  'FUNC 1000 40 0 Unit::step(int)',
  '1000 20 410 0',
  '1020 20 412 0',
  'FUNC a000 100 0 SDL_PumpEvents',
  'a000 100 77 1',
  'PUBLIC f000 0 main',
  'STACK CFI INIT 1000 40 .cfa: sp 16 +',
  '',
].join('\n');

describe('Breakpad lookup', () => {
  it('finds functions, lines and the nearest PUBLIC symbol', async () => {
    const path = join(work, 'glob2.sym');
    writeFileSync(path, SYM);
    const hits = await lookup(path, [0x1024, 0xa010, 0xf123, 0x10]);
    expect(hits.get(0x1024)).toEqual({
      function: 'Unit::step(int)',
      file: '/home/runner/work/glob2/glob2/src/unit/Unit.cpp',
      line: 412,
    });
    // Line records whose address starts with a hex letter still count.
    expect(hits.get(0xa010)?.line).toBe(77);
    expect(hits.get(0xf123)).toEqual({ function: 'main' });
    expect(hits.has(0x10)).toBe(false);
  });
});

describe('signatures', () => {
  const frame = (fn: string, file?: string, module = 'glob2'): CrashFrame => {
    const f = { module, offset: '0x1', function: fn, ...(file ? { file } : {}) };
    return { ...f, inGame: inGame(f) };
  };
  it('keeps the game code and leaves out libraries and the runtime', () => {
    expect(frame('Unit::step(int)', '/w/glob2/src/unit/Unit.cpp').inGame).toBe(true);
    expect(frame('SDL_PumpEvents', '/w/glob2/third_party/sdl/src/SDL.c').inGame).toBe(false);
    expect(frame('std::vector<int>::at(unsigned long)', '/usr/include/c++/14/vector').inGame).toBe(
      false,
    );
    expect(frame('abort').inGame).toBe(false);
    expect(frame('Unit::step(int)', undefined, 'libc.so.6').inGame).toBe(false);
    expect(frame('Map::get(int)', undefined, 'libmain.so').inGame).toBe(true);
  });
  it('joins the top three in-game functions and survives compiler clones', () => {
    expect(normalizeFunction('Team::update() [clone .cold]')).toBe('Team::update()');
    expect(normalizeFunction('Team::update.part.0')).toBe('Team::update');
    const frames = [
      frame('abort'),
      frame('Unit::step(int)'),
      frame('Team::update() [clone .cold]'),
      frame('SDL_PumpEvents', '/x/third_party/sdl/SDL.c'),
      frame('Game::syncStep()'),
      frame('Engine::run()'),
    ];
    expect(signature(frames)).toBe('Unit::step(int) | Team::update() | Game::syncStep()');
  });
  it('falls back to any named frame, then to the top offset', () => {
    expect(signature([frame('abort'), frame('raise')])).toBe('abort | raise');
    expect(signature([{ module: 'glob2', offset: '0x4f00', inGame: false }])).toBe('glob2+0x4f00');
    expect(signature([])).toBe('empty stack');
  });
  it('orders versions numerically', () => {
    expect(compareVersions('0.11.10', '0.11.9')).toBeGreaterThan(0);
    expect(compareVersions('0.11.8', '0.11.8.1')).toBeLessThan(0);
    expect(compareVersions('1.0.0', '1.0.0')).toBe(0);
  });
});

describe('minidump-stackwalk output', () => {
  it('expands inlined calls innermost first and reads the crash type', () => {
    const result = framesOf({
      crash_info: { type: 'SIGSEGV /SEGV_MAPERR' },
      crashing_thread: {
        frames: [
          {
            module: 'glob2',
            module_offset: '0x0000429e',
            function: 'Unit::step(int)',
            file: 'src/unit/Unit.cpp',
            line: 58,
            inlines: [{ function: 'Unit::hp() const', file: 'src/unit/Unit.h', line: 12 }],
          },
          { module: 'libc.so.6', module_offset: '0x11', function: null },
        ],
      },
    });
    expect(result.reason).toBe('SIGSEGV /SEGV_MAPERR');
    expect(result.frames.map((f) => f.function ?? f.module)).toEqual([
      'Unit::hp() const',
      'Unit::step(int)',
      'libc.so.6',
    ]);
    expect(result.frames[0]).toMatchObject({ offset: '0x0000429e', line: 12, inGame: true });
  });
  it('lays out symbols where minidump-stackwalk looks for them', () => {
    expect(breakpadPath('/c', 'test_app.pdb', 'ABC')).toBe('/c/test_app.pdb/ABC/test_app.sym');
    expect(breakpadPath('/c', 'glob2', 'ABC')).toBe('/c/glob2/ABC/glob2.sym');
    expect(breakpadPath('/c', 'libmain.so', 'ABC')).toBe('/c/libmain.so/ABC/libmain.so.sym');
    expect(safeModule('..')).toBe(false);
    expect(safeModule('a/b')).toBe(false);
    expect(safeModule('glob2.pdb')).toBe(true);
  });
});

describe('MetricKit', () => {
  const diagnostic = {
    version: '1.0.0',
    callStackTree: {
      callStackPerThread: true,
      callStacks: [
        { threadAttributed: false, callStackRootFrames: [] },
        {
          threadAttributed: true,
          callStackRootFrames: [
            {
              binaryUUID: '70B89F27-1634-3580-A695-57CDB41D7743',
              binaryName: 'glob2',
              offsetIntoBinaryTextSegment: 0x1024,
              address: 7170766264,
              subFrames: [
                {
                  binaryUUID: '70B89F27-1634-3580-A695-57CDB41D7743',
                  binaryName: 'glob2',
                  offsetIntoBinaryTextSegment: 0xf123,
                  subFrames: [
                    {
                      binaryUUID: '11111111-2222-3333-4444-555555555555',
                      binaryName: 'libsystem_kernel.dylib',
                      offsetIntoBinaryTextSegment: 0x99,
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
    diagnosticMetaData: { exceptionType: 1, signal: 11, appVersion: '0.11.8.1' },
  };
  it('reads the attributed thread from its root frame outwards', () => {
    expect(machoDebugId('70B89F27-1634-3580-A695-57CDB41D7743')).toBe(
      '70B89F2716343580A69557CDB41D77430',
    );
    const parsed = parseMetricKit(JSON.stringify(diagnostic));
    expect(parsed.reason).toBe('EXC_BAD_ACCESS / SIGSEGV');
    expect(parsed.frames.map((f) => [f.binary, f.offset])).toEqual([
      ['glob2', 0x1024],
      ['glob2', 0xf123],
      ['libsystem_kernel.dylib', 0x99],
    ]);
  });
  it('symbolicates frames of binaries whose symbols were uploaded', async () => {
    const path = join(work, 'mk.sym');
    writeFileSync(path, SYM);
    const cache = cacheOf({ 'breakpad:glob2:70B89F2716343580A69557CDB41D77430': path });
    const result = await symbolicateMetricKit(cache, JSON.stringify(diagnostic));
    expect(result.frames).toEqual([
      {
        module: 'glob2',
        offset: '0x1024',
        function: 'Unit::step(int)',
        file: '/home/runner/work/glob2/glob2/src/unit/Unit.cpp',
        line: 412,
        inGame: true,
      },
      { module: 'glob2', offset: '0xf123', function: 'main', inGame: true },
      { module: 'libsystem_kernel.dylib', offset: '0x99', inGame: false },
    ]);
    expect(cache.asked).toContain(
      'breakpad:libsystem_kernel.dylib:111111112222333344445555555555550',
    );
  });
});

describe('browser errors', () => {
  it('reads wasm frames from every browser format', () => {
    const chrome =
      'RuntimeError: unreachable\n    at Unit::step (wasm://wasm/glob2.wasm-0a1b2c3d:wasm-function[4127]:0x1a2b3c)\n    at glob2.wasm.wasm-function[88]:0x4f00';
    const firefox =
      'glob2.wasm:wasm-function[4127]:0x1A2B3C\n@https://glob2.online/play/glob2.js:1:2';
    expect(wasmFrames(chrome)).toEqual([
      { index: 4127, offset: '0x1a2b3c' },
      { index: 88, offset: '0x4f00' },
    ]);
    expect(wasmFrames(firefox)).toEqual([{ index: 4127, offset: '0x1a2b3c' }]);
  });
  it('names frames from the build symbol map', async () => {
    const path = join(work, 'glob2.js.symbols');
    writeFileSync(path, '0:abort\n88:Engine::run()\n4127:Unit::step(int)\n');
    const buildId = '9f86d081884c7d659a2feaa0c55ad015';
    const cache = cacheOf({ [`wasm:glob2.wasm:${buildId}`]: path });
    const result = await symbolicateWeb(
      cache,
      JSON.stringify({
        schema: 1,
        source: 'abort',
        message: 'RuntimeError: memory access out of bounds',
        stack:
          'at wasm-function[4127]:0x1a2b3c\nat wasm-function[88]:0x4f00\nat wasm-function[9]:0x10',
        buildId,
      }),
    );
    expect(result.reason).toBe('RuntimeError: memory access out of bounds');
    expect(result.frames).toEqual([
      { module: 'glob2.wasm', offset: '0x1a2b3c', function: 'Unit::step(int)', inGame: true },
      { module: 'glob2.wasm', offset: '0x4f00', function: 'Engine::run()', inGame: true },
      { module: 'glob2.wasm', offset: '0x10', inGame: false },
    ]);
  });
  it('refuses a payload that is not a WebErrorReport', async () => {
    await expect(symbolicateWeb(cacheOf({}), '{"schema":1}')).rejects.toThrow(/WebErrorReport/);
  });
});

describe('Java exceptions', () => {
  const TEXT = [
    'thread: SDLThread',
    'java.lang.IllegalStateException: surface lost',
    '\tat org.libsdl.app.SDLActivity.onNativeSurfaceChanged(SDLActivity.java:812)',
    '\tat org.globulation2.glob2.Glob2Activity.setPreparingAssets(Glob2Activity.java:41)',
    '\tat android.os.Handler.dispatchMessage(Handler.java:106)',
    '\tat java.lang.reflect.Method.invoke(Native Method)',
    'Caused by: java.io.IOException: closed',
    '\tat org.globulation2.glob2.CertificateTrust.verify(Unknown Source)',
    '',
  ].join('\n');

  it("reads the thrown exception's frames, not its causes", () => {
    const frames = javaFrames(TEXT);
    expect(frames).toHaveLength(4);
    expect(frames[0]).toEqual({
      module: 'java',
      offset: '0x0',
      function: 'org.libsdl.app.SDLActivity.onNativeSurfaceChanged',
      file: 'SDLActivity.java',
      line: 812,
      inGame: false,
    });
    expect(frames[1]?.inGame).toBe(true);
    expect(frames[3]).toEqual({
      module: 'java',
      offset: '0x0',
      function: 'java.lang.reflect.Method.invoke',
      inGame: false,
    });
  });

  it("names the exception and groups by the game's own frames", () => {
    const result = symbolicateJava(TEXT);
    expect(result.reason).toBe('java.lang.IllegalStateException: surface lost');
    expect(signature(result.frames)).toBe(
      'org.globulation2.glob2.Glob2Activity.setPreparingAssets',
    );
    expect(() => symbolicateJava('thread: main\nnothing here\n')).toThrow(UnreadableCrash);
  });
});

describe('non-fatal errors', () => {
  it('groups one failure across maps and matches', () => {
    expect(errorSignature('map-load', 'unknown terrain type 41 at (12, 7) in "Islands"')).toBe(
      errorSignature('map-load', "unknown terrain type 9 at (3, 0) in 'Lakes'"),
    );
    expect(errorSignature('network', 'relay closed 0x1f')).toBe('error network: relay closed #');
  });

  it('reads error.json and refuses anything else', () => {
    const result = readErrorReport(
      JSON.stringify({ schema: 1, category: 'map-load', message: 'unknown terrain type 41' }),
    );
    expect(result.frames).toEqual([]);
    expect(result.reason).toBe('map-load: unknown terrain type 41');
    expect(result.signature).toBe('error map-load: unknown terrain type #');
    expect(() => readErrorReport('{"schema":1,"category":"assets","message":""}')).toThrow(
      UnreadableCrash,
    );
  });
});
