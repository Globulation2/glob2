// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { encodeGeneratorDraft } from '@glob2/protocol';
import Settings, {
  defaultSettings,
  effectiveSettings,
} from '../src/pages/generatorStudio/Settings.tsx';
import Preview from '../src/pages/generatorStudio/Preview.tsx';
afterEach(cleanup);
const source = encodeGeneratorDraft({
  manifest: JSON.stringify({
    controls: [
      { id: 'wet', label: 'Wetland', kind: 'toggle', default: 1 },
      { id: 'shape', label: 'Shape', kind: 'choice', choices: ['round', 'long'], default: 0 },
      {
        id: 'scale',
        label: 'Scale',
        kind: 'range',
        minimum: 1,
        maximum: 5,
        default: 2,
        powerOfTwo: true,
      },
    ],
  }),
  script: 'export function generate(c){}',
});
it('uses engine dimension exponents and manifest defaults, dropping removed controls', () => {
  const change = vi.fn();
  render(<Settings source={source} value={defaultSettings} onChange={change} />);
  expect(screen.getByLabelText('Width').textContent).toContain('128 tiles');
  expect((screen.getByLabelText('Wetland') as HTMLInputElement).checked).toBe(true);
  fireEvent.change(screen.getByLabelText('Width'), { target: { value: '8' } });
  expect(change.mock.calls[0]?.[0].params.width).toBe(8);
  expect(
    effectiveSettings(source, {
      ...defaultSettings,
      params: { ...defaultSettings.params, obsolete: 99 },
    }).params,
  ).toEqual({ ...defaultSettings.params, wet: 1, shape: 0, scale: 2 });
  expect(
    effectiveSettings(encodeGeneratorDraft({ manifest: '{', script: '' }), defaultSettings),
  ).toEqual(defaultSettings);
});
it('keeps a frozen preview independent of settings and ignores stale messages', () => {
  const result = vi.fn();
  const run = {
    runId: '11111111-1111-4111-8111-111111111111',
    revision: 3,
    source: 'package',
    draftHash: 'a'.repeat(64),
    settings: defaultSettings,
  };
  render(<Preview run={run} onResult={result} />);
  const frame = screen.getByTitle('Generator preview revision 3') as HTMLIFrameElement;
  const send = (data: Record<string, unknown>) =>
    act(() =>
      window.dispatchEvent(
        new MessageEvent('message', {
          origin: location.origin,
          source: frame.contentWindow,
          data: {
            channel: 'glob2-generator-studio',
            version: 1,
            runId: run.runId,
            revision: 3,
            ...data,
          },
        }),
      ),
    );
  send({ type: 'generated', revision: 2, success: true, playable: true, seconds: 1 });
  expect(result).not.toHaveBeenCalled();
  send({ type: 'generated', success: true, playable: true, seconds: 1 });
  expect(result).not.toHaveBeenCalled();
  send({
    type: 'generated',
    success: true,
    playable: false,
    seconds: 1,
    telemetry: [],
    packageHash: 'a'.repeat(64),
    worldFingerprint: 'b'.repeat(64),
    simVersion: 'frozen-engine',
    checksum: 42,
  });
  expect(screen.getByText('Editor-only terrain cannot launch a colony game.')).toBeTruthy();
  expect(
    (screen.getByRole('button', { name: 'Watch AI play' }) as HTMLButtonElement).disabled,
  ).toBe(true);
  expect(JSON.parse(result.mock.calls[0]?.[0] as string).settings.seed).toBe(19);
  send({ type: 'complete', tick: 64, result: 'ended' });
  expect(JSON.parse(result.mock.calls.at(-1)?.[0] as string)).toMatchObject({
    revision: 3,
    packageHash: 'a'.repeat(64),
    worldFingerprint: 'b'.repeat(64),
    simVersion: 'frozen-engine',
    checksum: 42,
    tick: 64,
    result: 'ended',
    settings: defaultSettings,
  });
});
it('exposes bounded refusal reports without enabling a playtest', () => {
  const result = vi.fn();
  const run = {
    runId: '11111111-1111-4111-8111-111111111111',
    revision: 3,
    source: 'package',
    draftHash: 'a'.repeat(64),
    settings: defaultSettings,
  };
  render(<Preview run={run} onResult={result} />);
  const frame = screen.getByTitle('Generator preview revision 3') as HTMLIFrameElement;
  act(() =>
    window.dispatchEvent(
      new MessageEvent('message', {
        origin: location.origin,
        source: frame.contentWindow,
        data: {
          channel: 'glob2-generator-studio',
          version: 1,
          runId: run.runId,
          revision: 3,
          type: 'generated',
          success: false,
          seconds: 0.5,
          diagnostic: 'No room for colonies',
          telemetry: [],
        },
      }),
    ),
  );
  expect(screen.getByRole('status').textContent).toBe('No room for colonies');
  expect(JSON.parse(result.mock.calls[0]?.[0] as string).success).toBe(false);
});
