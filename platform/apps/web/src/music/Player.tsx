import { useEffect, useRef, useState } from 'react';
import type { MusicRelease } from '@glob2/protocol';
export const MOODS = ['Calm', 'Building', 'Combat'];
export function MusicPlayer({ release }: { release: MusicRelease }) {
  const engine = useRef<{ worker: Worker; context: AudioContext; node: AudioWorkletNode } | null>(
    null,
  );
  const [playing, setPlaying] = useState(false),
    [ready, setReady] = useState(false);
  const [position, setPosition] = useState(0),
    [weights, setWeights] = useState([1, 0, 0]);
  const [fade, setFade] = useState(0.3715),
    [blend, setBlend] = useState(0),
    [automatic, setAutomatic] = useState(false);
  const [error, setError] = useState(''),
    [loading, setLoading] = useState(false);
  const alive = useRef(true);
  useEffect(() => {
    alive.current = true;
    const hide = () => {
      if (document.hidden) {
        engine.current?.worker.postMessage({ command: 0, value: 0 });
        void engine.current?.context.suspend();
        setPlaying(false);
      }
    };
    document.addEventListener('visibilitychange', hide);
    return () => {
      alive.current = false;
      document.removeEventListener('visibilitychange', hide);
      engine.current?.worker.terminate();
      engine.current?.node.disconnect();
      void engine.current?.context.close();
      engine.current = null;
    };
  }, []);
  const command = (command: number, value = 0) =>
    engine.current?.worker.postMessage({ command, value });
  function fail(message: string) {
    engine.current?.worker.terminate();
    engine.current?.node.disconnect();
    void engine.current?.context.close();
    engine.current = null;
    setError(message);
    setLoading(false);
    setPlaying(false);
    setReady(false);
  }
  async function togglePlay() {
    if (!engine.current) {
      setLoading(true);
      setError('');
      const context = new AudioContext();
      try {
        await context.resume();
        await context.audioWorklet.addModule('/music/output-worklet.js');
        if (!alive.current) {
          await context.close();
          return;
        }
        const node = new AudioWorkletNode(context, 'glob2-music-output', {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2],
        });
        node.connect(context.destination);
        const worker = new Worker('/music/decode-worker.js', { type: 'module' });
        engine.current = { context, node, worker };
        context.onstatechange = () => {
          if (context.state !== 'running' && alive.current) {
            command(0, 0);
            setPlaying(false);
          }
        };
        worker.onerror = () => {
          fail('The music decoder could not be loaded.');
        };
        worker.onmessage = (event) => {
          if (!alive.current) return;
          const data = event.data as {
            ready?: boolean;
            error?: string;
            position?: number;
            weights?: number[];
          };
          if (data.error) {
            fail(data.error);
          }
          if (data.ready) {
            setReady(true);
            setLoading(false);
            setPlaying(true);
            command(0, 1);
          }
          if (data.position !== undefined) setPosition(data.position);
          if (data.weights) setWeights(data.weights);
        };
        const channel = new MessageChannel();
        node.port.postMessage({ port: channel.port1 }, [channel.port1]);
        worker.postMessage({ tracks: release.tracks, port: channel.port2 }, [channel.port2]);
      } catch (e) {
        await context.close();
        fail(String(e));
      }
    } else {
      try {
        await engine.current.context.resume();
        command(0, playing ? 0 : 1);
        setPlaying(!playing);
      } catch (error) {
        fail(String(error));
      }
    }
  }
  const duration = release.frames / 48000;
  return (
    <section className="music-player" aria-label="Synchronized music player">
      <div className="music-moods">
        {MOODS.map((mood, i) => (
          <button
            key={mood}
            disabled={!ready}
            onClick={() => command(1, i)}
            className={`mood-${i}`}
            aria-label={`Crossfade to ${mood}`}
          >
            {mood}
            <small>{Math.round((weights[i] ?? 0) * 100)}%</small>
          </button>
        ))}
      </div>
      <div className="music-waveforms">
        {release.tracks.map((track, i) => (
          <div key={track.mood} className={`mood-${i}`}>
            <span>{MOODS[i]}</span>
            <svg
              viewBox="0 0 512 40"
              preserveAspectRatio="none"
              aria-label={`${MOODS[i]} waveform`}
            >
              {track.waveform.map((peak, x) => (
                <line
                  key={x}
                  x1={x}
                  x2={x}
                  y1={20 - peak * 20}
                  y2={20 + peak * 20}
                  stroke="currentColor"
                />
              ))}
              <line
                x1={(position / duration) * 512}
                x2={(position / duration) * 512}
                y1="0"
                y2="40"
                stroke="white"
                strokeWidth="2"
              />
            </svg>
          </div>
        ))}
      </div>
      <label>
        Playback position{' '}
        <input
          type="range"
          min="0"
          max={duration}
          step="0.1"
          value={position}
          disabled={!ready}
          onChange={(e) => {
            setPosition(+e.target.value);
            command(2, +e.target.value);
          }}
        />
      </label>
      <div className="music-player-transport">
        <button onClick={() => void togglePlay()} disabled={loading}>
          {loading ? 'Loading music…' : playing ? 'Pause' : 'Play'}
        </button>
        <span>
          {Math.floor(position / 60)}:{String(Math.floor(position % 60)).padStart(2, '0')} /{' '}
          {Math.floor(duration / 60)}:{String(Math.floor(duration % 60)).padStart(2, '0')} · loops
          continuously
        </span>
      </div>
      <details>
        <summary>Test crossfades</summary>
        <label>
          Fade duration: {fade.toFixed(2)} seconds
          <input
            type="range"
            min="0"
            max="10"
            step="0.01"
            value={fade}
            disabled={!ready}
            onChange={(e) => {
              setFade(+e.target.value);
              command(3, +e.target.value);
            }}
          />
        </label>
        <label>
          Manual blend · Calm → Building → Combat
          <input
            type="range"
            min="0"
            max="2"
            step="0.01"
            value={blend}
            disabled={!ready}
            onChange={(e) => {
              setBlend(+e.target.value);
              command(4, +e.target.value);
            }}
          />
        </label>
        <label>
          <input
            type="checkbox"
            checked={automatic}
            disabled={!ready}
            onChange={(e) => {
              setAutomatic(e.target.checked);
              command(5, Number(e.target.checked));
            }}
          />{' '}
          Audition each mood for eight seconds
        </label>
        <button
          disabled={!ready}
          onClick={() => {
            setFade(0.3715);
            setBlend(0);
            setAutomatic(false);
            command(6);
          }}
        >
          Reset to game behavior
        </button>
      </details>
      {error && <p role="alert">{error}</p>}
    </section>
  );
}
