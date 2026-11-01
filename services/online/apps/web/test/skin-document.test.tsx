// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import type * as ApiModule from '../src/api.ts';
import { ApiError, request } from '../src/api.ts';
import { useSkinDocument } from '../src/skins/useSkinDocument.ts';
vi.mock('../src/api.ts', async (importOriginal) => ({
  ...(await importOriginal<typeof ApiModule>()),
  request: vi.fn(),
}));
vi.mock('../src/skins/atlas.ts', () => ({
  ATLAS_SIZE: 512,
  decodeMaterials: async () => new Uint8Array(512 * 512),
  encodeMaterials: () => 'data:image/png;base64,AAAA',
}));
let decodeImage = () => Promise.resolve();
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((done) => {
    resolve = done;
  });
  return { promise, resolve };
}
const revision = '12345678-1234-1234-1234-123456789abc';
const draft = {
  revision,
  skinId: revision,
  appliedRevision: null,
  appliedVersionId: null,
  name: 'Account design',
  buildingColor: 0xff0000,
  swarmMesh: 'classic' as const,
  imageBase64: 'AAAA',
  materialBase64: 'AAAA',
};
beforeEach(() => {
  decodeImage = () => Promise.resolve();
  vi.stubGlobal(
    'Image',
    class {
      src = '';
      width = 512;
      height = 512;
      decode() {
        return decodeImage();
      }
    },
  );
  vi.stubGlobal(
    'ImageData',
    class {
      data: Uint8ClampedArray;
      constructor(data: Uint8ClampedArray) {
        this.data = data;
      }
    },
  );
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({
    fillRect: vi.fn(),
    drawImage: vi.fn(),
    putImageData: vi.fn(),
    getImageData: () => ({ data: new Uint8ClampedArray(512 * 512 * 4).fill(255) }),
  } as unknown as CanvasRenderingContext2D);
  vi.spyOn(HTMLCanvasElement.prototype, 'toDataURL').mockReturnValue('data:image/png;base64,AAAA');
});
afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.mocked(request).mockReset();
});
async function documentHook() {
  const hook = renderHook(() => useSkinDocument('account-a'));
  await waitFor(() => expect(hook.result.current.hydrated).toBe(true));
  return hook;
}
it('keeps edits made while library images are loading', async () => {
  const image = deferred<undefined>();
  decodeImage = () => image.promise;
  const { result } = await documentHook();
  act(() => {
    void result.current.openDesign(draft);
  });
  act(() => result.current.edit({ swarmViewAngle: 123 }));
  await act(async () => {
    image.resolve(undefined);
    await image.promise;
  });
  await waitFor(() => expect(result.current.busy).toBe(false));
  expect(result.current.data.swarmViewAngle).toBe(123);
  expect(result.current.data.skinId).toBeUndefined();
  expect(result.current.message).toContain('Your changes were kept');
});
it('serializes saving and submits edits made during an in-flight save', async () => {
  const { result } = await documentHook();
  await act(() => result.current.openDesign(draft));
  const response = deferred<{ revision: string }>();
  vi.mocked(request)
    .mockReturnValueOnce(response.promise)
    .mockResolvedValueOnce({ revision: 'second' });
  act(() => result.current.edit({ name: 'First edit' }));
  let saved!: Promise<void>;
  act(() => {
    saved = result.current.flush();
  });
  act(() => result.current.edit({ name: 'Second edit' }));
  await act(async () => {
    response.resolve({ revision: 'first' });
    await saved;
  });
  expect(vi.mocked(request).mock.calls.map((call) => call[2]?.body)).toMatchObject([
    { revision, name: 'First edit' },
    { revision: 'first', name: 'Second edit' },
  ]);
  expect(result.current.status).toBe('Saved');
  expect(result.current.hasChanges).toBe(false);
});
it('does not report offline work as saved and recovers pending edits', async () => {
  const first = await documentHook();
  await act(() => first.result.current.openDesign(draft));
  act(() => first.result.current.edit({ name: 'Pending work' }));
  vi.mocked(request).mockRejectedValueOnce(new TypeError('Failed to fetch'));
  await act(async () => {
    await expect(first.result.current.flush()).rejects.toThrow('Failed to fetch');
  });
  expect(first.result.current.status).toContain('Could not save');
  first.unmount();
  const second = await documentHook();
  await act(() => second.result.current.openDesign(draft));
  expect(second.result.current.data.name).toBe('Pending work');
  expect(second.result.current.hasChanges).toBe(true);
});
it('stops autosaving on conflicts and preserves both copies until a choice', async () => {
  const { result } = await documentHook();
  await act(() => result.current.openDesign(draft));
  act(() => result.current.edit({ name: 'My changes' }));
  vi.mocked(request).mockRejectedValueOnce(new ApiError(409, undefined));
  await act(async () => {
    await expect(result.current.flush()).rejects.toThrow();
  });
  expect(result.current.conflict).toBe(true);
  expect(result.current.data.name).toBe('My changes');
  await act(() => result.current.openDesign({ ...draft, name: 'Account changes' }, true));
  expect(result.current.data.name).toBe('Account changes');
  expect(result.current.conflict).toBe(false);
  expect(result.current.hasChanges).toBe(false);
});
it('saves before using the exact acknowledged revision in game', async () => {
  const { result } = await documentHook();
  await act(() => result.current.openDesign(draft));
  act(() => result.current.edit({ name: 'Use this' }));
  vi.mocked(request).mockResolvedValueOnce({ revision: 'saved' }).mockResolvedValueOnce({});
  const applied = vi.fn();
  await act(() => result.current.useInGame(applied));
  expect(vi.mocked(request).mock.calls[1]).toMatchObject([
    'POST',
    `/api/v1/skins/designs/${revision}/use`,
    { body: { revision: 'saved' } },
  ]);
  expect(applied).toHaveBeenCalledOnce();
});
it('retries an uncertain creation with the same id rather than making another design', async () => {
  const { result } = await documentHook();
  vi.mocked(request)
    .mockRejectedValueOnce(new TypeError('Connection lost'))
    .mockResolvedValueOnce({ design: draft });
  await act(() => result.current.newDesign('New design'));
  await act(() => result.current.newDesign('New design'));
  expect(vi.mocked(request).mock.calls[0]?.[2]?.body).toEqual(
    vi.mocked(request).mock.calls[1]?.[2]?.body,
  );
  expect(result.current.data.skinId).toBe(draft.skinId);
});
it('retains the recovery failure status instead of reporting Ready', async () => {
  localStorage.setItem('glob2-skin-draft-v2:account-a:recovery', '{invalid');
  const { result } = await documentHook();
  expect(result.current.status).toBe('Draft recovery failed');
  expect(result.current.message).not.toBe('');
});
it('paints the current document after a same-event gesture cancellation', async () => {
  const { result } = await documentHook();
  act(() => {
    result.current.paint((data) => {
      data.colour[0] = 12;
    });
    result.current.finish(true);
    result.current.paint((data) => {
      data.colour[1] = 34;
    });
    result.current.finish();
  });
  expect(result.current.data.colour[0]).toBe(255);
  expect(result.current.data.colour[1]).toBe(34);
  act(() => result.current.history(true));
  expect(result.current.data.colour[1]).toBe(255);
  expect(result.current.canUndo).toBe(false);
});
