// SPDX-License-Identifier: GPL-3.0-or-later
importScripts('asset-video-runtime.js');

let runtime;
self.onmessage = async ({data: {id, bytes, width, height, count}}) => {
  const size = width * height * count * 4;
  try {
    if (![width, height, count].every(n => Number.isSafeInteger(n) && n > 0) ||
        width > 2048 || height > 2048 || count > 16 || size > 64 * 1024 * 1024 ||
        !(bytes instanceof ArrayBuffer) || !bytes.byteLength || bytes.byteLength > 16 * 1024 * 1024)
      throw Error('Invalid asset video dimensions or payload');
    const decoder = await (runtime ??= createAssetVideoRuntime());
    const input = decoder._malloc(bytes.byteLength);
    const output = decoder._malloc(size);
    try {
      if (!input || !output) throw Error('Insufficient asset video decoder memory');
      decoder.HEAPU8.set(new Uint8Array(bytes), input);
      if (!decoder._glob2_decode_asset_video(input, bytes.byteLength, width, height, count, output, size))
        throw Error('Asset video decode failed');
      const rgba = decoder.HEAPU8.slice(output, output + size).buffer;
      self.postMessage({id, rgba}, [rgba]);
    } finally {
      decoder._free(output);
      decoder._free(input);
    }
  } catch (error) {
    self.postMessage({id, error: error.message});
  }
};
