import { useCallback, useEffect, useRef, useState } from 'react';
import type { MusicRelease } from '@glob2/protocol';

export const GAME_FADE_SECONDS = 17833 / 48000;
export interface PlaybackSnapshot {
  position: number;
  mood: number;
  volume: number;
  muted: boolean;
  playing: boolean;
}
export type PlaybackSettings = Partial<PlaybackSnapshot>;
type Status = 'idle' | 'loading' | 'playing' | 'paused' | 'error';
type Mode = 'mood' | 'manual' | 'automatic';
interface Engine {
  context: AudioContext;
  node?: AudioWorkletNode;
  gain?: GainNode;
  worker?: Worker;
  ready: boolean;
}

/** The decoder stays bounded; all UI commands share this one audible clock. */
export function useMusicPlayback(
  release: MusicRelease,
  initial: PlaybackSettings = {},
  onSnapshot?: (snapshot: PlaybackSnapshot) => void,
) {
  const duration = release.frames / 48000;
  const [state, setState] = useState(() => ({
    status: 'idle' as Status,
    ready: false,
    position: Math.max(0, Math.min(initial.position ?? 0, duration)),
    mood: Math.max(0, Math.min(2, initial.mood ?? 0)),
    volume: Math.max(0, Math.min(1, initial.volume ?? 0.8)),
    muted: initial.muted ?? false,
    weights: [1, 0, 0],
    mode: 'mood' as Mode,
    fade: GAME_FADE_SECONDS,
    blend: initial.mood ?? 0,
    error: '',
  }));
  const live = useRef(state);
  const engine = useRef<Engine | null>(null);
  const alive = useRef(false);
  const wantsPlayback = useRef(false);
  const resumeInitially = useRef(initial.playing ?? false);
  const update = useCallback((patch: Partial<typeof state>) => {
    live.current = { ...live.current, ...patch };
    if (alive.current) setState(live.current);
  }, []);
  const command = useCallback((command: number, value = 0) => {
    engine.current?.worker?.postMessage({ command, value });
  }, []);
  const dispose = useCallback(() => {
    const old = engine.current;
    engine.current = null;
    old?.worker?.terminate();
    old?.node?.disconnect();
    old?.gain?.disconnect();
    if (old && old.context.state !== 'closed') void old.context.close().catch(() => {});
  }, []);
  const fail = useCallback(
    (error: unknown) => {
      dispose();
      wantsPlayback.current = false;
      update({
        ready: false,
        status: 'error',
        error: String(error),
        mode: 'mood',
        fade: GAME_FADE_SECONDS,
        blend: live.current.mood,
      });
    },
    [dispose, update],
  );
  const pause = useCallback(() => {
    wantsPlayback.current = false;
    command(5, 0);
    if (live.current.mode === 'automatic') command(1, live.current.mood);
    command(0, 0);
    engine.current?.node?.port.postMessage({ snapshot: true });
    const owned = engine.current;
    if (owned && owned.context.state !== 'closed')
      void owned.context.suspend().catch((error: unknown) => {
        if (engine.current === owned) fail(error);
      });
    update({
      status: engine.current?.ready ? 'paused' : live.current.status,
      mode: live.current.mode === 'automatic' ? 'mood' : live.current.mode,
    });
  }, [command, fail, update]);
  const play = useCallback(async () => {
    if (live.current.status === 'loading') return;
    wantsPlayback.current = true;
    update({ error: '', status: 'loading' });
    let current = engine.current;
    try {
      if (!current) {
        current = { context: new AudioContext(), ready: false };
        engine.current = current;
        const owned = current;
        const context = owned.context;
        await context.resume();
        if (!alive.current || engine.current !== owned) return;
        await context.audioWorklet.addModule('/music/output-worklet.js');
        if (!alive.current || engine.current !== owned) return;
        const node = new AudioWorkletNode(context, 'glob2-music-output', {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2],
        });
        owned.node = node;
        const gain = context.createGain();
        owned.gain = gain;
        gain.gain.value = live.current.muted ? 0 : live.current.volume;
        node.connect(gain);
        gain.connect(context.destination);
        const worker = new Worker('/music/decode-worker.js', { type: 'module' });
        owned.worker = worker;
        context.onstatechange = () => {
          if (engine.current === owned && owned.ready && context.state !== 'running') {
            wantsPlayback.current = false;
            command(0, 0);
            command(5, 0);
            if (live.current.mode === 'automatic') command(1, live.current.mood);
            node.port.postMessage({ snapshot: true });
            update({
              status: 'paused',
              mode: live.current.mode === 'automatic' ? 'mood' : live.current.mode,
            });
          }
        };
        node.port.onmessage = (event: MessageEvent<{ position: number; weights: number[] }>) => {
          if (!alive.current || engine.current !== owned) return;
          const { position, weights } = event.data;
          const dominant = weights.indexOf(Math.max(...weights));
          update({
            position,
            weights,
            ...(live.current.mode === 'automatic' ? { mood: dominant } : {}),
          });
        };
        worker.onerror = () => {
          if (engine.current === owned) fail('The music decoder could not be loaded.');
        };
        worker.onmessage = (event: MessageEvent<{ ready?: boolean; error?: string }>) => {
          if (!alive.current || engine.current !== owned) return;
          if (event.data.error) {
            fail(event.data.error);
            return;
          }
          if (event.data.ready) {
            owned.ready = true;
            resumeInitially.current = false;
            command(2, live.current.position);
            command(4, live.current.mood);
            command(3, live.current.fade);
            if (live.current.mode === 'automatic') {
              command(6);
              command(5, 1);
            }
            const start = wantsPlayback.current && !document.hidden && context.state === 'running';
            command(0, Number(start));
            update({ ready: true, status: start ? 'playing' : 'paused' });
            if (!start) void context.suspend().catch(fail);
          }
        };
        const channel = new MessageChannel();
        node.port.postMessage({ port: channel.port1 }, [channel.port1]);
        worker.postMessage(
          { tracks: release.tracks, frames: release.frames, port: channel.port2 },
          [channel.port2],
        );
      } else {
        const owned = current;
        await owned.context.resume();
        if (!alive.current || engine.current !== owned) return;
        const start = wantsPlayback.current && !document.hidden;
        command(0, Number(start));
        update({ ready: true, status: start ? 'playing' : 'paused' });
      }
    } catch (error) {
      if (alive.current && engine.current === current) fail(error);
    }
  }, [command, fail, release.frames, release.tracks, update]);
  useEffect(() => {
    alive.current = true;
    const hide = () => {
      if (document.hidden) pause();
    };
    document.addEventListener('visibilitychange', hide);
    if (resumeInitially.current && !document.hidden) {
      void play();
    }
    return () => {
      alive.current = false;
      wantsPlayback.current = false;
      document.removeEventListener('visibilitychange', hide);
      dispose();
      live.current = { ...live.current, status: 'idle', ready: false };
    };
  }, [dispose, pause, play]);
  useEffect(() => {
    onSnapshot?.({
      position: state.position,
      mood: state.mood,
      volume: state.volume,
      muted: state.muted,
      playing: state.status === 'playing',
    });
  }, [onSnapshot, state.position, state.mood, state.volume, state.muted, state.status]);

  return {
    ...state,
    duration,
    toggle: () => (state.status === 'playing' ? pause() : void play()),
    retry: () => void play(),
    selectMood: (mood: number) => {
      command(5, 0);
      // Applying a mood while paused must also clear a previous manual blend.
      if (state.status !== 'playing') command(4, mood);
      else command(1, mood);
      update({
        mood,
        mode: 'mood',
        blend: mood,
        ...(state.status !== 'playing'
          ? { weights: [0, 1, 2].map((i) => Number(i === mood)) }
          : {}),
      });
    },
    seek: (position: number) => {
      const bounded = Math.max(0, Math.min(position, duration));
      update({ position: bounded });
      command(2, bounded);
    },
    setVolume: (volume: number) => {
      update({ volume, muted: false });
      if (engine.current?.gain) engine.current.gain.gain.value = volume;
    },
    toggleMute: () => {
      const muted = !live.current.muted;
      update({ muted });
      if (engine.current?.gain) engine.current.gain.gain.value = muted ? 0 : live.current.volume;
    },
    toggleAutomatic: () => {
      if (state.mode === 'automatic') {
        command(5, 0);
        command(1, live.current.mood);
        update({ mode: 'mood' });
      } else {
        command(6);
        command(5, 1);
        update({ mode: 'automatic', mood: 0, blend: 0, fade: GAME_FADE_SECONDS });
        if (state.status !== 'playing') void play();
      }
    },
    setFade: (fade: number) => {
      command(5, 0);
      command(3, fade);
      if (state.mode === 'automatic') command(1, live.current.mood);
      update({ fade, mode: state.mode === 'automatic' ? 'mood' : state.mode });
    },
    setBlend: (blend: number) => {
      command(5, 0);
      command(4, blend);
      update({ blend, mode: 'manual', mood: Math.round(blend) });
    },
    restore: () => {
      command(6);
      update({ mode: 'mood', mood: 0, blend: 0, fade: GAME_FADE_SECONDS, weights: [1, 0, 0] });
    },
  };
}
