import { beforeEach, expect, it, vi } from 'vitest';
import type * as AiSdk from 'ai';
import { CodingProvider } from '../src/coding-studio/provider.ts';
import { generatorEdit } from '../src/generator-studio/domain.ts';
import { decodeGeneratorDraft } from '@glob2/protocol';
const { streamText } = vi.hoisted(() => ({ streamText: vi.fn() }));
vi.mock('ai', async (original) => ({ ...(await original<typeof AiSdk>()), streamText }));
const provider = new CodingProvider('test-key', 'test-model', generatorEdit);
beforeEach(() => streamText.mockReset());

it.each([
  { name: 'discussion', calls: [], finish: 'stop', error: false },
  {
    name: 'complete edit',
    calls: [{ toolName: 'replace_generator', input: { manifest: '{}', script: 'source' } }],
    finish: 'tool-calls',
    error: false,
  },
  {
    name: 'ambiguous edit',
    calls: Array.from({ length: 2 }, () => ({
      toolName: 'replace_generator',
      input: { manifest: '{}', script: 'source' },
    })),
    finish: 'tool-calls',
    error: true,
  },
  {
    name: 'invalid edit',
    calls: [{ toolName: 'replace_generator', input: {}, invalid: true }],
    finish: 'tool-calls',
    error: true,
  },
  {
    name: 'missing script',
    calls: [{ toolName: 'replace_generator', input: { manifest: '{}' } }],
    finish: 'tool-calls',
    error: true,
  },
  {
    name: 'invalid manifest',
    calls: [{ toolName: 'replace_generator', input: { manifest: '{', script: 'source' } }],
    finish: 'tool-calls',
    error: true,
  },
  {
    name: 'truncated edit',
    calls: [{ toolName: 'replace_generator', input: { manifest: '{}', script: 'partial' } }],
    finish: 'length',
    error: true,
  },
])(
  'meters $name while only applying one complete valid edit',
  async ({ calls, finish, error, name }) => {
    streamText.mockReturnValue({
      textStream: (async function* () {
        yield 'Explanation';
      })(),
      usage: Promise.resolve({
        inputTokens: 10,
        outputTokens: 20,
        inputTokenDetails: { cacheReadTokens: 2 },
      }),
      toolCalls: Promise.resolve(calls),
      finishReason: Promise.resolve(finish),
      response: Promise.resolve({ id: 'response-id' }),
    });
    const result = await provider.generate(
      'system',
      'prompt',
      1024,
      new AbortController().signal,
      async () => {},
    );
    expect(result.usage).toEqual({ input: 10, cachedInput: 2, output: 20 });
    expect(!!result.editError).toBe(error);
    expect(result.source ? decodeGeneratorDraft(result.source) : undefined).toEqual(
      name === 'complete edit' ? { manifest: '{}', script: 'source' } : undefined,
    );
  },
);
