/* A separate WebAssembly memory contains only a copied, team-filtered observation. */
'use strict';
importScripts('hive-runtime.js');
let used = false;
self.onmessage = async event => {
  if (used) return;
  used = true;
  try {
    if (typeof event.data !== 'string' || event.data.length > 64 * 1024 * 1024) throw new Error('Oversized observation');
    const runtime = await createHiveRuntime({print: () => {}, printErr: () => {}});
    const size = runtime.lengthBytesUTF8(event.data) + 1;
    const pointer = runtime._malloc(size);
    if (!pointer) throw new Error('Worker memory exhausted');
    let result;
    try {
      runtime.stringToUTF8(event.data, pointer, size);
      result = runtime.ccall('glob2_hive_invoke', 'string', ['number'], [pointer]);
    } finally { runtime._free(pointer); }
    if (result.length > 2 * 1024 * 1024) throw new Error('Oversized result');
    self.postMessage(JSON.parse(result));
  } catch {
    self.postMessage({ok: false});
  }
  self.close();
};
