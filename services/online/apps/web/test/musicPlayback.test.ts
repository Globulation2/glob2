import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { describe, expect, it } from 'vitest';

const workerSource = readFileSync(
  new URL('../public/music/decode-worker.js', import.meta.url),
  'utf8',
).replace(/^import .*;$/m, '');
const outputSource = readFileSync(
  new URL('../public/music/output-worklet.js', import.meta.url),
  'utf8',
);
type Packet = {
  pcm: Float32Array;
  generation: number;
  position: number;
  weights: number[];
  duration: number;
};

it('preserves decoded look-ahead through pause/resume and resets only when seeking', () => {
  const packets: (Packet | { reset: boolean })[] = [];
  let position = 0;
  const context = {
    onmessage: undefined,
    postMessage: () => {},
    decoder: {
      HEAP16: new Int16Array(2048),
      _music_command: (command: number, value: number) => {
        if (command === 2) position = value;
      },
      _music_render: () => {
        position += 1024 / 48000;
        return 0;
      },
      _music_position: () => position,
      _music_weight: (mood: number) => Number(mood === 0),
      _music_failed: () => false,
    },
    audio: { postMessage: (packet: Packet) => packets.push(packet) },
  };
  runInNewContext(
    workerSource + '\nmodule=decoder; port=audio; ready=true; duration=10; credits=8;',
    context,
  );
  const send = (command: number, value: number) =>
    runInNewContext(`onmessage({data:{command:${command},value:${value}}})`, context);
  send(0, 1);
  expect(packets).toHaveLength(8);
  const lookAheadPosition = position;
  send(0, 0);
  send(0, 1);
  expect(packets).toHaveLength(8);
  expect(position).toBe(lookAheadPosition);
  send(2, 4);
  expect(packets[8]).toMatchObject({ reset: true, position: 4, generation: 1 });
  expect(packets).toHaveLength(17);
});

describe.each([44100, 48000, 96000])('audible clock at %i Hz', (rate) => {
  it('tracks consumed samples through resampling, underrun, pause and seek', () => {
    const reports: { position: number }[] = [];
    const context = {
      sampleRate: rate,
      AudioWorkletProcessor: class {
        port = {
          onmessage: (_: unknown) => {},
          postMessage: (report: { position: number }) => reports.push(report),
        };
      },
      registerProcessor: (_: string, processor: new () => unknown) => {
        context.output = new processor();
      },
      output: undefined as unknown,
    };
    runInNewContext(outputSource, context);
    const output = context.output as {
      port: { onmessage: (event: unknown) => void };
      process: (input: never[], output: Float32Array[][]) => void;
    };
    const audio = { onmessage: (_: unknown) => {}, start: () => {}, postMessage: () => {} };
    output.port.onmessage({ data: { port: audio } });
    for (let i = 0; i < 8; ++i)
      audio.onmessage({
        data: {
          pcm: new Float32Array(2048).fill(i / 8),
          position: (i * 1024) / 48000,
          duration: 10,
          weights: [1, 0, 0],
          generation: 0,
        },
      });
    output.process([], [[new Float32Array(128), new Float32Array(128)]]);
    output.port.onmessage({ data: { snapshot: true } });
    expect(reports.at(-1)?.position).toBeCloseTo(128 / rate, 8);
    // Suspending AudioContext stops process calls; queued PCM and position survive.
    output.port.onmessage({ data: { snapshot: true } });
    expect(reports.at(-1)?.position).toBeCloseTo(128 / rate, 8);
    output.process([], [[new Float32Array(128), new Float32Array(128)]]);
    output.port.onmessage({ data: { snapshot: true } });
    expect(reports.at(-1)?.position).toBeCloseTo(256 / rate, 8);
    audio.onmessage({ data: { reset: true, generation: 1, position: 4 } });
    output.process([], [[new Float32Array(128), new Float32Array(128)]]);
    output.port.onmessage({ data: { snapshot: true } });
    expect(reports.at(-1)?.position).toBe(4);
  });
});
