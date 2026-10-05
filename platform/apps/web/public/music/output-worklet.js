// One resampler after all three moods have been mixed on the 48 kHz timeline.
// Position belongs to the consumer, never to the decoder's look-ahead queue.
class MusicOutput extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.offset = 0;
    this.fraction = 0;
    this.generation = 0;
    this.position = 0;
    this.weights = [1, 0, 0];
    this.reportFrames = 0;
    this.port.onmessage = (e) => {
      if (e.data.snapshot) {
        this.report();
        return;
      }
      this.audio = e.data.port;
      this.audio.onmessage = (event) => {
        const data = event.data;
        if (data.reset) {
          this.queue = [];
          this.offset = 0;
          this.fraction = 0;
          this.generation = data.generation;
          this.position = data.position;
          this.report();
        } else if (data.generation === this.generation && this.queue.length < 8)
          this.queue.push(data);
      };
      this.audio.start();
    };
  }
  report() {
    this.port.postMessage({ position: this.position, weights: this.weights });
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!out?.[0]) return true;
    for (let i = 0; i < out[0].length; i++) {
      const packet = this.queue[0];
      if (!packet) break;
      const next = this.offset + 1 < 1024 ? packet : this.queue[1];
      if (!next) break;
      const nextOffset = this.offset + 1 < 1024 ? this.offset + 1 : 0;
      for (let channel = 0; channel < out.length; channel++) {
        const a = packet.pcm[this.offset * 2 + channel],
          b = next.pcm[nextOffset * 2 + channel];
        out[channel][i] = a + (b - a) * this.fraction;
      }
      this.position =
        (packet.position + (this.offset + this.fraction + 48000 / sampleRate) / 48000) %
        packet.duration;
      this.weights = packet.weights;
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
      if (++this.reportFrames >= sampleRate / 30) {
        this.reportFrames = 0;
        this.report();
      }
    }
    return true;
  }
}
registerProcessor('glob2-music-output', MusicOutput);
