import { afterEach, expect, it, vi } from 'vitest';
import { OpenAIMusic, ProviderRejected } from '../src/provider.ts';

afterEach(() => vi.restoreAllMocks());

it.each([
  { text: 'A quiet cave with distant electronic pulses.', brief: 'Somber electronic cave music' },
  { action: 'write', text: 'Compose the score', value: 'SCORE = ...' },
])('requires JSON for the music discussion and composition protocol', async (message) => {
  const fetch = vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    new Response(
      JSON.stringify({
        id: 'response-test',
        status: 'completed',
        usage: { input_tokens: 100, output_tokens: 50 },
        output: [{ content: [{ type: 'output_text', text: JSON.stringify(message) }] }],
      }),
    ),
  );
  const reply = await new OpenAIMusic('test-key').text(
    'configured-model',
    'Respond with JSON',
    4000,
    new AbortController().signal,
  );
  const [url, request] = fetch.mock.calls[0]!;
  expect(url).toBe('https://api.openai.com/v1/responses');
  expect(JSON.parse(request!.body as string)).toMatchObject({
    model: 'configured-model',
    store: false,
    text: { format: { type: 'json_object' } },
    max_output_tokens: 4000,
  });
  expect(JSON.parse(reply.text)).toEqual(message);
  expect(reply.responseId).toBe('response-test');
});

it('does not dispatch an already-aborted provider call', async () => {
  const fetch = vi.spyOn(globalThis, 'fetch');
  await expect(
    new OpenAIMusic('test').text('test', 'prompt', 4000, AbortSignal.abort()),
  ).rejects.toBeInstanceOf(ProviderRejected);
  expect(fetch).not.toHaveBeenCalled();
});
