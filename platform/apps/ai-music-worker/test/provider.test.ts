import { afterEach, expect, it, vi } from 'vitest';
import { OpenAIMusic, ProviderRejected } from '../src/provider.ts';

afterEach(() => vi.restoreAllMocks());

it.each([
  {
    format: 'discussion' as const,
    message: { text: 'A quiet cave with distant electronic pulses.', brief: 'Somber cave music' },
    required: ['text', 'brief'],
  },
  {
    format: 'action' as const,
    message: { action: 'write', text: 'Compose the score', value: 'SCORE = ...' },
    required: ['action', 'text', 'value'],
  },
])('requires the $format protocol schema', async ({ format, message, required }) => {
  const fetch = vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    new Response(
      JSON.stringify({
        id: 'response-test',
        status: 'completed',
        usage: { input_tokens: 100, output_tokens: 50 },
        output: [
          {
            type: 'message',
            phase: 'commentary',
            content: [{ type: 'output_text', text: 'I will compose a quiet cave score.' }],
          },
          {
            type: 'message',
            phase: 'commentary',
            content: [{ type: 'output_text', text: '{"action":"read","value":"source"}' }],
          },
          {
            type: 'message',
            phase: 'final_answer',
            content: [{ type: 'output_text', text: JSON.stringify(message) }],
          },
        ],
      }),
    ),
  );
  const reply = await new OpenAIMusic('test-key').text(
    'configured-model',
    'Respond with JSON',
    4000,
    new AbortController().signal,
    format,
  );
  const [url, request] = fetch.mock.calls[0]!;
  expect(url).toBe('https://api.openai.com/v1/responses');
  expect(JSON.parse(request!.body as string)).toMatchObject({
    model: 'configured-model',
    store: false,
    text: {
      format: {
        type: 'json_schema',
        name: `music_${format}`,
        strict: true,
        schema: { type: 'object', required, additionalProperties: false },
      },
    },
    max_output_tokens: 4000,
  });
  expect(JSON.parse(reply.text)).toEqual(message);
  expect(reply.responseId).toBe('response-test');
});

it('accepts the final untagged message from models without phase metadata', async () => {
  const message = { action: 'done', text: 'Ready', value: '' };
  vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    new Response(
      JSON.stringify({
        id: 'response-test',
        status: 'completed',
        usage: {},
        output: [
          { type: 'message', content: [{ type: 'output_text', text: JSON.stringify(message) }] },
        ],
      }),
    ),
  );
  const reply = await new OpenAIMusic('test').text(
    'test',
    'Respond with JSON',
    4000,
    new AbortController().signal,
  );
  expect(JSON.parse(reply.text)).toEqual(message);
});

it('does not execute a commentary-only action as a final answer', async () => {
  vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    new Response(
      JSON.stringify({
        id: 'response-test',
        status: 'completed',
        usage: {},
        output: [
          {
            type: 'message',
            phase: 'commentary',
            content: [{ type: 'output_text', text: '{"action":"write","value":"preamble"}' }],
          },
        ],
      }),
    ),
  );
  await expect(
    new OpenAIMusic('test').text('test', 'Respond with JSON', 4000, new AbortController().signal),
  ).rejects.toBeInstanceOf(ProviderRejected);
});

it('does not dispatch an already-aborted provider call', async () => {
  const fetch = vi.spyOn(globalThis, 'fetch');
  await expect(
    new OpenAIMusic('test').text('test', 'prompt', 4000, AbortSignal.abort()),
  ).rejects.toBeInstanceOf(ProviderRejected);
  expect(fetch).not.toHaveBeenCalled();
});
