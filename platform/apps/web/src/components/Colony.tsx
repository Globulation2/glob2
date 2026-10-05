// Real menu-colony footage. The poster stays visible until playback succeeds.
import { useEffect, useRef, useState } from 'react';
import { COLONY } from '../art.tsx';

export function ColonyHero() {
  const life = useRef<HTMLDivElement>(null);
  const video = useRef<HTMLVideoElement>(null);
  const [paused, setPaused] = useState(false);
  const [offscreen, setOffscreen] = useState(false);
  const [reduced, setReduced] = useState(true);
  const [playing, setPlaying] = useState(false);
  useEffect(() => {
    const query = window.matchMedia('(prefers-reduced-motion: reduce)');
    const change = () => setReduced(query.matches);
    change();
    query.addEventListener('change', change);
    return () => query.removeEventListener('change', change);
  }, []);
  useEffect(() => {
    const element = life.current;
    if (!element || typeof IntersectionObserver === 'undefined') return;
    const observer = new IntersectionObserver(([entry]) =>
      setOffscreen(!(entry?.isIntersecting ?? true)),
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    const element = video.current;
    if (!element) return;
    const update = () => {
      if (paused || offscreen || reduced || document.hidden) element.pause();
      else void element.play().catch(() => setPlaying(false));
    };
    update();
    document.addEventListener('visibilitychange', update);
    return () => {
      document.removeEventListener('visibilitychange', update);
      element.pause();
    };
  }, [paused, offscreen, reduced]);
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
          className={playing && !reduced ? 'colony-video visible' : 'colony-video'}
          src={reduced ? undefined : COLONY.video}
          poster={COLONY.large}
          muted
          loop
          playsInline
          preload="none"
          tabIndex={-1}
          onPlaying={() => setPlaying(true)}
          onError={() => setPlaying(false)}
        />
      </div>
      {!reduced && playing && (
        <button type="button" className="motion-toggle small" onClick={() => setPaused(!paused)}>
          {paused ? '▶ Let the globs roam' : '❚❚ Pause the globs'}
        </button>
      )}
    </>
  );
}
