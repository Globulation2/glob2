import { Wordmark as SharedWordmark } from '@glob2/design-system/react';
import { t } from './i18n.tsx';
import { useLocale } from './i18n.tsx';
// The game's own artwork (data/gfx, data/highres), compressed by
// art/build_art.py. Vite fingerprints these files, so they cache forever.
import algae from '@glob2/design-system/assets/algae.webp';
import clearingFlag from '@glob2/design-system/assets/clearing-flag.webp';
import colony1600 from '@glob2/design-system/assets/colony-1600.webp';
import colony960 from '@glob2/design-system/assets/colony-960.webp';
import colonyVideo from '@glob2/design-system/assets/colony-loop.mp4';
import explorationFlag from '@glob2/design-system/assets/exploration-flag.webp';
import fruit from '@glob2/design-system/assets/fruit.webp';
import globIcon from '@glob2/design-system/assets/glob-64.png';
import hospital from '@glob2/design-system/assets/hospital.webp';
import inn from '@glob2/design-system/assets/inn.webp';
import papyrus from '@glob2/design-system/assets/papyrus.webp';
import pool from '@glob2/design-system/assets/pool.webp';
import racetrack from '@glob2/design-system/assets/racetrack.webp';
import school from '@glob2/design-system/assets/school.webp';
import stone from '@glob2/design-system/assets/stone.webp';
import swarm from '@glob2/design-system/assets/swarm.webp';
import warFlag from '@glob2/design-system/assets/war-flag.webp';
import warrior from '@glob2/design-system/assets/warrior.webp';
import wood from '@glob2/design-system/assets/wood.webp';
import worker from '@glob2/design-system/assets/worker.webp';

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
  warrior,
  wood,
  worker,
};
export type ArtName = keyof typeof ART;

export const GLOB_ICON = globIcon;
export const COLONY = { small: colony960, large: colony1600, video: colonyVideo };

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
  useLocale();
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
  label = t('Globulation 2'),
  className = '',
}: {
  label?: string | null;
  className?: string;
}) {
  useLocale();
  return <SharedWordmark label={label} className={className} />;
}
