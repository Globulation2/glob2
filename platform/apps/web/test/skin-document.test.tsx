// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import { request } from '../src/api.ts';
import { useSkinDocument, type Skin } from '../src/skins/useSkinDocument.ts';
vi.mock('../src/api.ts', () => ({ request: vi.fn() }));
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
  name: 'Account design',
  buildingColor: 0xff0000,
  swarmMesh: 'classic',
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
it('keeps edits made while an account draft is being restored', async () => {
  const response = deferred<{ draft: typeof draft }>();
  vi.mocked(request).mockReturnValueOnce(response.promise);
  const { result } = await documentHook();
  act(() => {
    void result.current.accountDraft(false);
  });
  act(() => result.current.edit({ name: 'Newer local work' }));
  await act(async () => {
    response.resolve({ draft });
    await response.promise;
  });
  await waitFor(() => expect(result.current.busy).toBe(false));
  expect(result.current.data.name).toBe('Newer local work');
  expect(result.current.message).toContain('Your changes were kept');
});
it('keeps edits made while library images are loading', async () => {
  const image = deferred<undefined>();
  decodeImage = () => image.promise;
  const { result } = await documentHook();
  act(() => {
    void result.current.openDesign({
      id: 'version',
      kind: 'custom',
      skinId: revision,
      name: 'Library design',
      buildingColor: 0,
      swarmMesh: 'classic',
    } as Skin);
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
it('does not attach a completed publication to a newer separate design', async () => {
  const response = deferred<{ skinId: string }>();
  vi.mocked(request).mockReturnValueOnce(response.promise);
  const { result } = await documentHook();
  const published = vi.fn();
  act(() => {
    void result.current.publish(published);
  });
  act(() => result.current.edit({ name: 'Different design', skinId: undefined }));
  await act(async () => {
    response.resolve({ skinId: revision });
    await response.promise;
  });
  await waitFor(() => expect(result.current.busy).toBe(false));
  expect(result.current.data.skinId).toBeUndefined();
  expect(result.current.data.name).toBe('Different design');
  expect(result.current.message).toContain('newer draft changes were kept');
  expect(published).toHaveBeenCalledOnce();
});
it('recovers the known account revision without bypassing server conflicts', async () => {
  vi.mocked(request).mockResolvedValueOnce({ revision });
  const first = await documentHook();
  await act(() => first.result.current.accountDraft(true));
  first.unmount();
  const second = await documentHook();
  vi.mocked(request).mockRejectedValueOnce(new Error('Your account draft changed.'));
  await act(() => second.result.current.accountDraft(true));
  expect(vi.mocked(request).mock.calls[1]?.[2]?.body).toMatchObject({ revision });
  expect(second.result.current.message).toBe('Your account draft changed.');
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
