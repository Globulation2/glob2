// One resampler after all three moods have been mixed on the 48 kHz timeline.
class MusicOutput extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.offset = 0;
    this.fraction = 0;
    this.generation = 0;
    this.port.onmessage = (e) => {
      this.audio = e.data.port;
      this.audio.onmessage = (event) => {
        const data = event.data;
        if (data.reset) {
          this.queue = [];
          this.offset = 0;
          this.fraction = 0;
          this.generation = data.generation;
        } else if (data.generation === this.generation && this.queue.length < 8)
          this.queue.push(data.pcm);
      };
      this.audio.start();
    };
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!out?.[0]) return true;
    for (let i = 0; i < out[0].length; i++) {
      const block = this.queue[0];
      if (!block) break;
      const next = this.offset + 1 < 1024 ? block : this.queue[1];
      if (!next) break;
      const nextOffset = this.offset + 1 < 1024 ? this.offset + 1 : 0;
      for (let channel = 0; channel < out.length; channel++) {
        const a = block[this.offset * 2 + channel],
          b = next[nextOffset * 2 + channel];
        out[channel][i] = a + (b - a) * this.fraction;
      }
      this.fraction += 48000 / sampleRate;
      while (this.fraction >= 1) {
        this.fraction--;
        this.offset++;
        if (this.offset === 1024) {
          this.offset = 0;
          this.queue.shift();
          this.audio.postMessage({ generation: this.generation });
        }
      }
    }
    return true;
  }
}
registerProcessor('glob2-music-output', MusicOutput);
