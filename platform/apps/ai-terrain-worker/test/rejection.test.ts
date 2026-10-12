import { afterEach, expect, it, vi } from 'vitest';
import { newPackage } from '@glob2/protocol';
import type { TerrainStudio, RequestRow } from '@glob2/terrain-studio';
import type { AgentBlobs } from '@glob2/engine/blobs';
import { Pipeline, type Validator } from '../src/pipeline.ts';
import { Attempts, ProviderRejected } from '../src/provider.ts';
afterEach(() => vi.restoreAllMocks());
it('ends a definite provider rejection without making design repair calls', async () => {
  vi.spyOn(Attempts.prototype, 'run').mockImplementation(
    async (_row, _stage, _model, _input, call) => call(),
  );
  const finish = vi.fn();
  const studio = {
    recoverUncertain: vi.fn(),
    claim: vi.fn(),
    stage: vi.fn(),
    reserveBuild: vi.fn(),
    text: vi.fn(),
    finish,
  };
  const base = newPackage('11111111-1111-4111-8111-111111111111');
  studio.claim.mockResolvedValue({
    id: 'test',
    input: { base, brief: 'snow', messages: [], submission: { references: [] } },
  } as unknown as RequestRow);
  const provider = {
    text: vi.fn(async () => ({
      usage: {},
      text: JSON.stringify({
        action: 'build',
        text: 'Snow',
        brief: 'snow',
        title: 'Snow',
        description: '',
        entries: [
          {
            kind: 'terrain',
            operation: 'upsert',
            key: 'snow',
            name: 'Snow',
            preset: 'ice',
            propertiesJson: '{}',
            yieldsJson: '{}',
            presentationJson: '{}',
            allowedResourceKeys: [],
            regenerateArt: true,
            artPrompt: 'snow',
            decorPrompt: '',
            animationFrames: 1,
          },
        ],
      }),
    })),
    image: vi.fn(async () => {
      throw new ProviderRejected(
        'Provider rejected this request (HTTP 400): unsupported background',
      );
    }),
  };
  const pipeline = new Pipeline(
    studio as unknown as TerrainStudio,
    {} as AgentBlobs,
    provider,
    {} as Validator,
    {
      enabled: true,
      salesEnabled: false,
      textModel: 'test',
      imageModel: 'test',
      pipelineVersion: 'terrain-v1',
      providerCallsPerDay: 20,
    },
    '/nonexistent-test-assets',
    'python3',
    'test',
  );
  await pipeline.tick();
  expect(provider.text).toHaveBeenCalledTimes(1);
  expect(provider.image).toHaveBeenCalledTimes(1);
  expect(finish).toHaveBeenCalledWith(
    expect.anything(),
    undefined,
    'Provider rejected this request (HTTP 400): unsupported background',
  );
  expect(studio.text.mock.calls.some(([, message]) => String(message).includes('Repair'))).toBe(
    false,
  );
});
