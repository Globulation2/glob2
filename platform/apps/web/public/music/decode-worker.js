// The worker owns compressed bytes and decoders; PCM is capped at eight blocks.
import createDecoder from './decoder.js';
let module,
  port,
  ready = false,
  credits = 0,
  generation = 0;
async function initialize(tracks, audioPort) {
  module = await createDecoder();
  port = audioPort;
  for (let mood = 0; mood < 3; mood++) {
    const response = await fetch(tracks[mood].url, { credentials: 'same-origin' });
    if (!response.ok) throw new Error('A music track is no longer available.');
    const reader = response.body?.getReader();
    if (!reader) throw new Error('Music response is empty.');
    const chunks = [];
    let length = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      length += value.length;
      if (length > 16 * 1024 * 1024) {
        await reader.cancel();
        throw new Error('Music track exceeds the preview limit.');
      }
      chunks.push(value);
    }
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.length;
    }
    chunks.length = 0;
    if (bytes.length > 16 * 1024 * 1024) throw new Error('Music track exceeds the preview limit.');
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (b) =>
      b.toString(16).padStart(2, '0'),
    ).join('');
    if (hash !== tracks[mood].sha256) throw new Error('Music download checksum failed.');
    const ptr = module._malloc(bytes.length);
    if (!ptr) throw new Error('Not enough memory to preview this music.');
    module.HEAPU8.set(bytes, ptr);
    const ok = module._music_open(mood, ptr, bytes.length);
    module._free(ptr);
    if (!ok) throw new Error('Music tracks are invalid or have different lengths.');
  }
  port.onmessage = (event) => {
    if (event.data.generation === generation) {
      credits++;
      pump();
    }
  };
  port.start();
  ready = true;
  credits = 8;
  postMessage({ ready: true, decoderHeapBytes: module.HEAPU8.byteLength });
  pump();
}
function pump() {
  while (ready && credits > 0) {
    credits--;
    const ptr = module._music_render();
    if (module._music_failed()) {
      ready = false;
      postMessage({ error: 'Could not decode this music.' });
      return;
    }
    const pcm = new Float32Array(2048);
    for (let i = 0; i < 2048; i++) pcm[i] = module.HEAP16[ptr / 2 + i] / 32768;
    port.postMessage({ pcm, generation }, [pcm.buffer]);
  }
  if (module)
    postMessage({
      position: module._music_position(),
      weights: [0, 1, 2].map((i) => module._music_weight(i)),
    });
}
onmessage = (event) => {
  if (event.data.tracks)
    initialize(event.data.tracks, event.data.port).catch((error) =>
      postMessage({ error: error.message }),
    );
  else if (module) {
    module._music_command(event.data.command, event.data.value || 0);
    if (event.data.command === 2 || event.data.command === 0) {
      generation++;
      credits = 8;
      port.postMessage({ reset: true, generation });
    }
    pump();
  }
};
