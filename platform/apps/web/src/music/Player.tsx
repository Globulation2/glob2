import { useEffect, useRef, useState } from 'react';
import type { MusicRelease } from '@glob2/protocol';
export const MOODS = ['Calm', 'Building', 'Combat'];
const GAME_FADE_SECONDS = 17833 / 48000;
export function MusicPlayer({
  release,
  initialPosition = 0,
  onPosition,
}: {
  release: MusicRelease;
  initialPosition?: number;
  onPosition?: (seconds: number) => void;
}) {
  const engine = useRef<{ worker: Worker; context: AudioContext; node: AudioWorkletNode } | null>(
    null,
  );
  const [playing, setPlaying] = useState(false),
    [ready, setReady] = useState(false);
  const [position, setPosition] = useState(0),
    [weights, setWeights] = useState([1, 0, 0]);
  const [fade, setFade] = useState(GAME_FADE_SECONDS),
    [blend, setBlend] = useState(0),
    [automatic, setAutomatic] = useState(false);
  const [error, setError] = useState(''),
    [loading, setLoading] = useState(false);
  const alive = useRef(true);
  const wantsPlayback = useRef(false);
  useEffect(() => {
    alive.current = true;
    const hide = () => {
      if (document.hidden) {
        wantsPlayback.current = false;
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
    wantsPlayback.current = false;
    setError(message);
    setLoading(false);
    setPlaying(false);
    setReady(false);
    setPosition(0);
    setWeights([1, 0, 0]);
    setFade(GAME_FADE_SECONDS);
    setBlend(0);
    setAutomatic(false);
  }
  async function togglePlay() {
    if (!engine.current) {
      wantsPlayback.current = true;
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
            wantsPlayback.current = false;
            command(0, 0);
            node.port.postMessage({ snapshot: true });
            setPlaying(false);
          }
        };
        node.port.onmessage = (event: MessageEvent<{ position: number; weights: number[] }>) => {
          if (!alive.current) return;
          setPosition(event.data.position);
          onPosition?.(event.data.position);
          setWeights(event.data.weights);
        };
        worker.onerror = () => {
          fail('The music decoder could not be loaded.');
        };
        worker.onmessage = (event) => {
          if (!alive.current) return;
          const data = event.data as {
            ready?: boolean;
            error?: string;
          };
          if (data.error) {
            fail(data.error);
          }
          if (data.ready) {
            setReady(true);
            setLoading(false);
            command(2, Math.max(0, Math.min(initialPosition, release.frames / 48000)));
            const start = wantsPlayback.current && !document.hidden && context.state === 'running';
            setPlaying(start);
            command(0, Number(start));
            if (!start) void context.suspend();
          }
        };
        const channel = new MessageChannel();
        node.port.postMessage({ port: channel.port1 }, [channel.port1]);
        worker.postMessage(
          { tracks: release.tracks, frames: release.frames, port: channel.port2 },
          [channel.port2],
        );
      } catch (e) {
        await context.close();
        fail(String(e));
      }
    } else {
      try {
        if (playing) {
          wantsPlayback.current = false;
          await engine.current.context.suspend();
          command(0, 0);
          engine.current.node.port.postMessage({ snapshot: true });
          setPlaying(false);
        } else {
          wantsPlayback.current = true;
          await engine.current.context.resume();
          const start = wantsPlayback.current && !document.hidden;
          command(0, Number(start));
          setPlaying(start);
        }
      } catch (error) {
        fail(String(error));
      }
    }
  }
  const duration = release.frames / 48000;
  return (
    <section className="music-player" aria-label="Synchronized music player">
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
      {error && <p role="alert">{error}</p>}
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
            setFade(GAME_FADE_SECONDS);
            setBlend(0);
            setAutomatic(false);
            command(6);
          }}
        >
          Reset to game behavior
        </button>
      </details>
    </section>
  );
}
