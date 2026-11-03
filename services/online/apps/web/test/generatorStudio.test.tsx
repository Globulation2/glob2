// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { encodeGeneratorDraft } from '@glob2/protocol';
import Settings, {
  defaultSettings,
  effectiveSettings,
} from '../src/pages/generatorStudio/Settings.tsx';
import Preview from '../src/pages/generatorStudio/Preview.tsx';
import { LocaleProvider } from '../src/i18n.tsx';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import bg from '../../../packages/i18n/locales/bg.json';
afterEach(cleanup);
const source = encodeGeneratorDraft({
  manifest: JSON.stringify({
    controls: [
      { id: 'wet', label: 'Wetland', kind: 'toggle', default: 1 },
      {
        id: 'shape',
        label: 'Shape',
        kind: 'choice',
        choices: ['round', 'long'],
        default: 0,
      },
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
  send({
    type: 'generated',
    revision: 2,
    success: true,
    playable: true,
    seconds: 1,
  });
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

it('relocalizes retained failure and completion status without engine lifecycle or report changes', async () => {
  registerCatalog('bg', bg);
  await setLocale('en');
  try {
    const result = vi.fn();
    const run = {
      runId: '11111111-1111-4111-8111-111111111111',
      revision: 3,
      source: 'package',
      draftHash: 'a'.repeat(64),
      settings: defaultSettings,
    };
    const view = render(
      <LocaleProvider>
        <Preview run={run} onResult={result} />
      </LocaleProvider>,
    );
    const frame = view.container.querySelector('iframe')!;
    const post = vi.spyOn(frame.contentWindow!, 'postMessage');
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
    const switchLocale = async (locale: string, expected: string) => {
      const callbacks = result.mock.calls.length;
      await act(() => setLocale(locale));
      expect(screen.getByRole('status').textContent).toBe(expected);
      expect(post).not.toHaveBeenCalled(); // No stop or relaunch without a new engine event.
      expect(result.mock.calls.length).toBe(callbacks);
    };
    const generic = bg['Something went wrong. Please try again.'];
    send({
      type: 'generated',
      success: false,
      playable: false,
      seconds: 1,
      diagnostic: 'Fixed generator failure: exact-custom-id',
    });
    expect(screen.getByRole('status').textContent).toBe('Fixed generator failure: exact-custom-id');
    await switchLocale('bg', generic);
    await switchLocale('en', 'Fixed generator failure: exact-custom-id');
    expect(view.container.querySelector('pre')?.textContent).toContain(
      'Fixed generator failure: exact-custom-id',
    );
    expect(JSON.parse(result.mock.calls.at(-1)![0]).diagnostic).toBe(
      'Fixed generator failure: exact-custom-id',
    );
    send({ type: 'diagnostic', text: 'Exact original runtime diagnostic array entry' });
    send({ type: 'error', text: 'Exact runtime report {p0} %1' });
    expect(screen.getByRole('status').textContent).toBe('Exact runtime report {p0} %1');
    await switchLocale('bg', generic);
    expect(view.container.textContent).toContain('Exact runtime report {p0} %1');
    await switchLocale('en', 'Exact runtime report {p0} %1');
    await switchLocale('bg', generic);
    const errorReport = JSON.parse(result.mock.calls.at(-1)![0]);
    expect(errorReport.text).toBe('Exact runtime report {p0} %1');
    expect(errorReport.logs).toEqual(['Exact original runtime diagnostic array entry']);
    send({ type: 'complete', result: 'Exact completion result' });
    expect(screen.getByRole('status').textContent).toBe(bg.Finished);
    await switchLocale('en', 'Exact completion result');
    await switchLocale('bg', bg.Finished);
    const completed = JSON.parse(result.mock.calls.at(-1)![0]);
    expect(completed.result).toBe('Exact completion result');
    expect(completed.logs).toEqual(['Exact original runtime diagnostic array entry']);
    expect(errorReport.logs).toEqual(['Exact original runtime diagnostic array entry']);
  } finally {
    cleanup();
    await setLocale('en');
  }
});
