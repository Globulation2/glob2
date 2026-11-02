import { afterEach, expect, it, vi } from 'vitest';
import { OpenAIBuildings, ProviderRejected, ProviderUncertain } from '../src/provider.ts';
afterEach(() => vi.unstubAllGlobals());
const generate = () =>
  new OpenAIBuildings('test-key').image(
    'test-model',
    'a depot',
    [],
    true,
    new AbortController().signal,
  );
it('explains a definite image-parameter rejection without classifying it as uncertainty', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn(
      async () =>
        new Response(
          JSON.stringify({
            error: { message: 'Transparent background is not supported with this model.' },
          }),
          { status: 400 },
        ),
    ),
  );
  await expect(generate()).rejects.toThrow(ProviderRejected);
  await expect(generate()).rejects.toThrow('HTTP 400): Transparent background is not supported');
});
it('bounds malformed and oversized error responses', async () => {
  for (const body of ['not JSON', 'x'.repeat(17000)]) {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response(body, { status: 403 })),
    );
    await expect(generate()).rejects.toThrow('Provider refused this request (HTTP 403)');
  }
});
it('retains uncertain treatment for server failures and request timeouts', async () => {
  for (const status of [500, 503, 408]) {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response('{}', { status })),
    );
    await expect(generate()).rejects.toThrow(ProviderUncertain);
  }
});
