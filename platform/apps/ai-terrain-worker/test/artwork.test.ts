import { expect, it, vi } from 'vitest';
import { ArtworkInvalid, conversionError, prepareArtwork } from '../src/artwork.ts';
it('reports known conversion errors without subprocess paths or traceback', () => {
  const e = conversionError({
    stderr:
      'warning\nTraceback details /tmp/private\nValueError: Sprite touches a frame edge; regenerate with more padding\n',
  });
  expect(e).toBeInstanceOf(ArtworkInvalid);
  expect(e.message).toBe('Sprite touches a frame edge; regenerate with more padding');
  expect(conversionError({ stderr: 'FileNotFoundError: /private/path' }).message).toBe(
    'Artwork conversion could not complete.',
  );
});
it('corrects only images and returns the first valid processed sheet', async () => {
  const generate = vi.fn(async () => new Uint8Array([1]));
  const process = vi
    .fn()
    .mockRejectedValueOnce(new ArtworkInvalid('Missing resource/decor frame'))
    .mockResolvedValueOnce({ sheet: 'valid' });
  const repaired = vi.fn(async () => undefined);
  expect(await prepareArtwork(generate, process, repaired)).toEqual({ sheet: 'valid' });
  expect(generate.mock.calls.length).toBe(2);
  expect(repaired).toHaveBeenCalledWith(1, 'Missing resource/decor frame');
});
it('bounds correction calls and does not retry provider or infrastructure failures', async () => {
  const generate = vi.fn(async () => new Uint8Array([1]));
  const repaired = vi.fn(async () => undefined);
  await expect(
    prepareArtwork(
      generate,
      async () => {
        throw new ArtworkInvalid('Missing resource/decor frame');
      },
      repaired,
    ),
  ).rejects.toThrow('after two image repair passes');
  expect(generate).toHaveBeenCalledTimes(3);
  const failed = vi.fn(async () => {
    throw Error('Provider unavailable');
  });
  await expect(prepareArtwork(failed, async () => 1, repaired)).rejects.toThrow(
    'Provider unavailable',
  );
  expect(failed).toHaveBeenCalledTimes(1);
  const once = vi.fn(async () => new Uint8Array([1]));
  await expect(
    prepareArtwork(
      once,
      async () => {
        throw Error('Conversion worker unavailable');
      },
      repaired,
    ),
  ).rejects.toThrow('Conversion worker unavailable');
  expect(once).toHaveBeenCalledTimes(1);
});
