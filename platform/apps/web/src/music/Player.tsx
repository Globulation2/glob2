import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useEffect, type CSSProperties } from 'react';
import type { MusicRelease } from '@glob2/protocol';
import {
  useMusicPlayback,
  type PlaybackSettings,
  type PlaybackSnapshot,
} from './useMusicPlayback.ts';
export const MOODS = ['Calm', 'Building', 'Combat'];
const MOOD_DESCRIPTIONS = ['Room to breathe', 'A colony takes shape', 'Into the fray'];
export function MoodIcon({ mood }: { mood: number }) {
  useLocale();
  return (
    <svg
      viewBox="0 0 24 24"
      aria-hidden="true"
      className="music-mood-icon"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      {mood === 0 ? (
        <>
          <path d="M20 4C9 2 3 8 6 15c7 4 14-2 14-11Z" />
          <path d="m4 21 11-12M9 16v-5m0 5h5" />
        </>
      ) : mood === 1 ? (
        <>
          <path d="M4 21h16M6 21V10h12v11M4 10l8-7 8 7M10 21v-7h4v7" />
          <path d="M17 3v4" />
        </>
      ) : (
        <>
          <path d="m5 3 15 17M3 5l3-2 1 4M4 16l4 4m-3-1-2 2M19 3 4 20m17-15-3-2-1 4m3 9-4 4m3-1 2 2" />
        </>
      )}
    </svg>
  );
}
export function formatMusicTime(seconds: number) {
  return `${Math.floor(seconds / 60)}:${String(Math.floor(seconds % 60)).padStart(2, '0')}`;
}
export function MusicPlayer({
  release,
  initialPosition = 0,
  initialSettings,
  onPosition,
  onSnapshot,
}: {
  release: MusicRelease;
  initialPosition?: number;
  initialSettings?: PlaybackSettings;
  onPosition?: (seconds: number) => void;
  onSnapshot?: (snapshot: PlaybackSnapshot) => void;
}) {
  useLocale();
  const player = useMusicPlayback(
    release,
    { position: initialPosition, ...initialSettings },
    onSnapshot,
  );
  useEffect(() => {
    onPosition?.(player.position);
  }, [onPosition, player.position]);
  const transitioning =
    player.status === 'playing' &&
    player.mode === 'mood' &&
    (player.weights[player.mood] ?? 0) < 0.99;
  const progress = player.duration > 0 ? (player.position / player.duration) * 100 : 0;
  return (
    <section
      className="music-player"
      aria-label={t('Synchronized music player')}
      data-playing={player.status === 'playing'}
    >
      <div className="music-player-transport">
        <button
          className="primary big music-play"
          onClick={player.toggle}
          disabled={player.status === 'loading'}
        >
          <span aria-hidden="true">{player.status === 'playing' ? 'Ⅱ' : '▶'}</span>
          {player.status === 'loading'
            ? t('Loading music…')
            : player.status === 'playing'
              ? t('Pause')
              : t('Play music')}
        </button>
        <div className="music-clock">
          <strong>{formatMusicTime(player.position)}</strong>
          <span>
            <RichMessage
              source={' / {slot0}'}
              slots={{ slot0: formatMusicTime(player.duration) }}
            />
          </span>
          <small>{t('↻ Continuous loop')}</small>
        </div>
        <div className="music-volume">
          <button
            className="ghost small"
            onClick={player.toggleMute}
            aria-label={player.muted ? t('Unmute') : t('Mute')}
            aria-pressed={player.muted}
          >
            <svg
              viewBox="0 0 24 24"
              width="20"
              height="20"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.6"
              strokeLinecap="round"
              strokeLinejoin="round"
              aria-hidden="true"
            >
              <path d="M4 9h4l5-4v14l-5-4H4Z" />
              {player.muted ? (
                <path d="m17 9 5 6m0-6-5 6" />
              ) : (
                <>
                  <path d="M16 8c3 2 3 6 0 8" />
                  <path d="M19 5c5 4 5 10 0 14" />
                </>
              )}
            </svg>
          </button>
          <label>
            <span className="music-sr">{t('Volume')}</span>
            <input
              type="range"
              min="0"
              max="1"
              step="0.01"
              value={player.muted ? 0 : player.volume}
              onChange={(e) => player.setVolume(+e.target.value)}
            />
          </label>
        </div>
      </div>
      {player.status === 'error' && (
        <div className="music-playback-error">
          <p role="alert">
            <RichMessage
              source={'{slot0} Try again, or download the set to listen locally.'}
              slots={{ slot0: <strong>{t('Music couldn’t be loaded.')}</strong> }}
            />
          </p>
          <button onClick={player.retry}>{t('Retry')}</button>
          <details>
            <summary>{t('Technical details')}</summary>
            <p>{player.error}</p>
          </details>
        </div>
      )}
      <p className="music-player-intro">
        {t(
          'One soundtrack, three synchronized moods. Switch moods while listening to hear how the game adapts.',
        )}
      </p>
      <div className="music-moods" aria-label={t('Soundtrack mood')}>
        {MOODS.map((mood, i) => (
          <button
            key={mood}
            onClick={() => player.selectMood(i)}
            className={`mood-${i}`}
            aria-pressed={player.mode !== 'manual' && player.mood === i}
            aria-label={t('Crossfade to {value0}', { value0: mood })}
          >
            <MoodIcon mood={i} />
            <span>
              <strong>{mood}</strong>
              <small>{t(MOOD_DESCRIPTIONS[i])}</small>
            </span>
            <span className="music-mood-mark" aria-hidden="true">
              {player.mode !== 'manual' && player.mood === i ? '✓' : ''}
            </span>
          </button>
        ))}
      </div>
      <div className="music-audible-status" role="status">
        {player.mode === 'manual'
          ? t('Custom mood blend')
          : transitioning
            ? t('Transitioning to {value0}…', { value0: t(MOODS[player.mood]) })
            : player.status === 'playing'
              ? t('{value0} playing', { value0: t(MOODS[player.mood]) })
              : t('{value0} selected', { value0: t(MOODS[player.mood]) })}
      </div>
      <div className="music-timeline">
        <div className="music-waveform-labels" aria-hidden="true">
          {MOODS.map((mood) => (
            <span key={mood}>{mood}</span>
          ))}
        </div>
        <div
          className="music-waveforms"
          style={{ '--music-progress': `${progress}%` } as CSSProperties}
        >
          {release.tracks.map((track, i) => (
            <svg
              key={track.mood}
              className={`mood-${i}`}
              viewBox={`0 0 ${Math.max(1, track.waveform.length)} 40`}
              preserveAspectRatio="none"
              aria-label={t('{value0} waveform', { value0: MOODS[i] })}
              style={{ opacity: 0.35 + (player.weights[i] ?? 0) * 0.65 }}
            >
              {track.waveform.map((peak, x) => (
                <line
                  key={x}
                  x1={x}
                  x2={x}
                  y1={20 - peak * 18}
                  y2={20 + peak * 18}
                  stroke="currentColor"
                />
              ))}
            </svg>
          ))}
          <div className="music-playhead" aria-hidden="true" />
          <input
            className="music-waveform-seek"
            aria-label={t('Playback position')}
            aria-valuetext={formatMusicTime(player.position)}
            type="range"
            min="0"
            max={player.duration}
            step="0.1"
            value={player.position}
            onChange={(e) => player.seek(+e.target.value)}
          />
        </div>
      </div>
      <div className="music-timeline-caption">
        <span>{t('Drag the playhead to explore')}</span>
        <span>
          <RichMessage
            source={'{slot0} loop'}
            slots={{ slot0: formatMusicTime(player.duration) }}
          />
        </span>
      </div>
      <div className="music-game-preview">
        <div>
          <strong>{t('Hear it in the game')}</strong>
          <p>{t('Calm → Building → Combat → Calm · eight seconds per mood')}</p>
        </div>
        <button
          onClick={player.toggleAutomatic}
          disabled={player.status === 'loading'}
          aria-pressed={player.mode === 'automatic'}
        >
          {player.mode === 'automatic'
            ? t('Stop transition preview')
            : t('Preview game transitions')}
        </button>
      </div>
      {player.mode === 'automatic' && (
        <p className="music-preview-status" role="status">
          <RichMessage
            source={'Previewing game transitions · {slot0} · game fade timing'}
            slots={{ slot0: t(MOODS[player.mood]) }}
          />
        </p>
      )}
      <details className="music-advanced">
        <summary>{t('Advanced mixing')}</summary>
        <p>
          {t('Explore custom blends and fades. Restore game settings for the in-game transition.')}
        </p>
        <label>
          {t('Fade duration: ')}
          {player.fade.toFixed(2)} {t(' seconds')}
          <input
            type="range"
            min="0"
            max="10"
            step="0.01"
            value={player.fade}
            disabled={!player.ready}
            onChange={(e) => player.setFade(+e.target.value)}
          />
        </label>
        <label>
          {t('Manual blend · Calm → Building → Combat')}
          <input
            type="range"
            min="0"
            max="2"
            step="0.01"
            value={player.blend}
            disabled={!player.ready}
            onChange={(e) => player.setBlend(+e.target.value)}
          />
        </label>
        <div className="music-mix-readout">
          {MOODS.map((mood, i) => (
            <span key={mood}>
              {mood}{' '}
              <strong>
                <RichMessage
                  source={'{slot0}%'}
                  slots={{ slot0: Math.round((player.weights[i] ?? 0) * 100) }}
                />
              </strong>
            </span>
          ))}
        </div>
        <button disabled={!player.ready} onClick={player.restore}>
          {t('Restore game settings')}
        </button>
      </details>
    </section>
  );
}
