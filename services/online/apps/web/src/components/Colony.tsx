import { t, useLocale } from '../i18n.tsx';
import { useEffect, useRef, useState } from 'react';
import { mountColony } from '@glob2/design-system/colony';
import { COLONY } from '../art.tsx';

/** Application layout around the shared decoration lifecycle. */
export function ColonyHero() {
  const locale = useLocale();
  const life = useRef<HTMLDivElement>(null);
  const video = useRef<HTMLVideoElement>(null);
  const button = useRef<HTMLButtonElement>(null);
  const [playing, setPlaying] = useState(false);
  useEffect(() => {
    if (!video.current || !button.current || !life.current) return;
    return mountColony(video.current, button.current, {
      pause: t('❚❚ Pause the globs'),
      resume: t('▶ Let the globs roam'),
      container: life.current,
      onPlaying: setPlaying,
    });
  }, [locale]);
  return (
    <>
      <div ref={life} className="hero-art" aria-hidden="true">
        <img
          src={COLONY.large}
          srcSet={`${COLONY.small} 960w, ${COLONY.large} 1600w`}
          sizes="100vw"
          width={1600}
          height={900}
          alt=""
          fetchPriority="high"
          decoding="async"
        />
        <video
          ref={video}
          className={playing ? 'colony-video visible' : 'colony-video'}
          data-src={COLONY.video}
          poster={COLONY.large}
          muted
          loop
          playsInline
          preload="none"
          tabIndex={-1}
        />
      </div>
      <button ref={button} type="button" className="motion-toggle small" hidden>
        {t('❚❚ Pause the globs')}
      </button>
    </>
  );
}
