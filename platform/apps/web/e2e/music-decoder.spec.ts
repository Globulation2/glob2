import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { test, expect } from '@playwright/test';

test('production CSP permits worker Opus decoding while document evaluation stays blocked', async ({
  page,
}) => {
  // The existing trimmed fixture contains 4813 stereo frames of audible tones.
  const bytes = readFileSync(
    new URL('../../../../test/fixtures/audio/trimmed.opus', import.meta.url),
  );
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  await page
    .context()
    .route('**/__music-test.opus', (route) =>
      route.fulfill({ contentType: 'audio/ogg', body: bytes }),
    );
  await page.goto('/');
  const result = await page.evaluate(async (sha256) => {
    const wasmBlocked = await WebAssembly.compile(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]))
      .then(() => false)
      .catch(() => true);
    const pcm = await new Promise<boolean>((resolve, reject) => {
      const worker = new Worker('/music/decode-worker.js', { type: 'module' });
      const channel = new MessageChannel();
      const timeout = setTimeout(() => finish(new Error('Decoder did not produce PCM')), 15000);
      function finish(result: boolean | Error) {
        clearTimeout(timeout);
        worker.terminate();
        channel.port1.close();
        if (result instanceof Error) reject(result);
        else resolve(result);
      }
      worker.onerror = (event) => finish(new Error(event.message));
      worker.onmessage = (event) => {
        if (event.data.error) finish(new Error(event.data.error));
        if (event.data.ready) worker.postMessage({ command: 0, value: 1 });
      };
      channel.port1.onmessage = (event) => {
        if (event.data.pcm) {
          finish(
            Array.from(event.data.pcm as Float32Array).some((sample) => Math.abs(sample) > 0.01),
          );
        }
      };
      worker.postMessage(
        {
          tracks: [0, 1, 2].map(() => ({ url: '/__music-test.opus', sha256 })),
          frames: 4813,
          port: channel.port2,
        },
        [channel.port2],
      );
    });
    return { wasmBlocked, pcm };
  }, sha256);
  expect(result).toEqual({ wasmBlocked: true, pcm: true });
});
