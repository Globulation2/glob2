// The home page hero: the game's menu colony (data/gfx/menu-colony.png) with
// globs crossing it, drawn from the game's own walk and flight cycles. The
// animation is decoration only: it can be paused, stops when the hero is off
// screen, and stands still for viewers who ask for reduced motion.
import { useEffect, useRef, useState, type CSSProperties } from 'react';
import { COLONY, STRIPS } from '../art.tsx';
import { teamHue } from '../colors.ts';

// The unit artwork's own hue; hue-rotate() turns it into a team colour.
const SPRITE_HUE = 153;

interface Glob {
  kind: 'worker' | 'warrior' | 'explorer';
  west?: boolean;
  /** Lane, as a fraction of the hero's height. */
  y: number;
  /** Seconds to cross the screen, and the start offset (negative = already under way). */
  t: number;
  d: number;
  /** Team colour as on a 6-team map. */
  team: number;
  /** Where the glob stands when motion is reduced (fraction of the width). */
  park: number;
}

const GLOBS: Glob[] = [
  { kind: 'worker', y: 0.64, t: 46, d: -8, team: 3, park: 0.58 },
  { kind: 'worker', y: 0.7, t: 52, d: -30, team: 3, park: 0.66 },
  { kind: 'worker', west: true, y: 0.77, t: 40, d: -14, team: 0, park: 0.74 },
  { kind: 'warrior', y: 0.83, t: 58, d: -40, team: 0, park: 0.86 },
  { kind: 'worker', west: true, y: 0.58, t: 60, d: -50, team: 2, park: 0.9 },
  { kind: 'worker', y: 0.88, t: 44, d: -4, team: 4, park: 0.52 },
  { kind: 'explorer', west: true, y: 0.16, t: 34, d: -10, team: 3, park: 0.8 },
  { kind: 'explorer', west: true, y: 0.3, t: 42, d: -31, team: 1, park: 0.62 },
];

function stripOf(glob: Glob): string {
  if (glob.kind === 'explorer') return STRIPS.explorerWest;
  if (glob.kind === 'warrior') return STRIPS.warriorEast;
  return glob.west ? STRIPS.workerWest : STRIPS.workerEast;
}

export function ColonyHero() {
  const life = useRef<HTMLDivElement>(null);
  const [paused, setPaused] = useState(false);
  const [offscreen, setOffscreen] = useState(false);
  useEffect(() => {
    const element = life.current;
    if (!element || typeof IntersectionObserver === 'undefined') return;
    const observer = new IntersectionObserver(([entry]) =>
      setOffscreen(!(entry?.isIntersecting ?? true)),
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  return (
    <>
      <div className="hero-art">
        <img
          src={COLONY.large}
          srcSet={`${COLONY.small} 960w, ${COLONY.large} 1600w`}
          sizes="100vw"
          width={1600}
          height={900}
          alt="A Globulation 2 colony at work: glob workers around their swarm and inns, wheat fields and candy-coloured forests by the sea."
          fetchPriority="high"
          decoding="async"
        />
        <div
          ref={life}
          className={`colony-life${paused || offscreen ? ' paused' : ''}`}
          aria-hidden="true"
          data-testid="colony-life"
        >
          {GLOBS.map((glob, i) => {
            const west = glob.kind === 'explorer' ? true : Boolean(glob.west);
            // Westbound strips play the eastbound path backwards; explorers
            // only have a westbound strip, so they always fly west.
            const style = {
              '--y': `${glob.y * 100}%`,
              '--t': `${glob.t}s`,
              '--d': `${glob.d}s`,
              '--park': `${glob.park * 100}vw`,
              '--hue': `${teamHue(glob.team, 6) - SPRITE_HUE}deg`,
              '--strip': `url(${stripOf(glob)})`,
              ...(glob.kind === 'explorer' ? { '--f': '0.5s' } : {}),
              ...(glob.kind === 'warrior' ? { '--f': '0.9s' } : {}),
            } as CSSProperties;
            return glob.kind === 'explorer' ? (
              <div key={i} className="flyer west" style={style}>
                <span>
                  <span />
                </span>
              </div>
            ) : (
              <div key={i} className={`walker${west ? ' west' : ''}`} style={style}>
                <span />
              </div>
            );
          })}
        </div>
      </div>
      <button type="button" className="motion-toggle small" onClick={() => setPaused(!paused)}>
        {paused ? '▶ Let the globs roam' : '❚❚ Pause the globs'}
      </button>
    </>
  );
}
