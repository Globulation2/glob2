// The game's own artwork (data/gfx, data/highres), compressed by
// art/build_art.py. Vite fingerprints these files, so they cache forever.
import algae from './art/algae.webp';
import clearingFlag from './art/clearing-flag.webp';
import colony1600 from './art/colony-1600.webp';
import colony960 from './art/colony-960.webp';
import explorationFlag from './art/exploration-flag.webp';
import explorerWest from './art/explorer-west.webp';
import fruit from './art/fruit.webp';
import globIcon from './art/glob-64.png';
import hospital from './art/hospital.webp';
import inn from './art/inn.webp';
import papyrus from './art/papyrus.webp';
import pool from './art/pool.webp';
import racetrack from './art/racetrack.webp';
import school from './art/school.webp';
import stone from './art/stone.webp';
import swarm from './art/swarm.webp';
import warFlag from './art/war-flag.webp';
import warriorEast from './art/warrior-east.webp';
import wood from './art/wood.webp';
import workerEast from './art/worker-east.webp';
import workerWest from './art/worker-west.webp';

export const ART = {
  algae,
  clearingFlag,
  explorationFlag,
  fruit,
  hospital,
  inn,
  papyrus,
  pool,
  racetrack,
  school,
  stone,
  swarm,
  warFlag,
  wood,
};
export type ArtName = keyof typeof ART;

export const GLOB_ICON = globIcon;
export const COLONY = { small: colony960, large: colony1600 };
export const STRIPS = { workerEast, workerWest, warriorEast, explorerWest };

/** A decorative sprite from the game (buildings, flags, resources). */
export function GameArt({
  name,
  size = 48,
  className,
  alt = '',
}: {
  name: ArtName;
  size?: number;
  className?: string;
  alt?: string;
}) {
  return (
    <img
      src={ART[name]}
      width={size}
      height={size}
      alt={alt}
      className={className}
      loading="lazy"
      decoding="async"
      draggable={false}
    />
  );
}

/** The game's wordmark, drawn from two masks so it follows the theme. */
export function Wordmark({
  label = 'Globulation 2',
  className = '',
}: {
  label?: string | null;
  className?: string;
}) {
  return label ? (
    <span className={`wordmark ${className}`} role="img" aria-label={label} />
  ) : (
    <span className={`wordmark ${className}`} aria-hidden="true" />
  );
}
