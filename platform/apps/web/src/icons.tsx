// Interface icons: the game's own Tabler outline set (datasrc/icons/tabler, pinned
// and hash-checked by its manifest.json), shared with the native UI. Add an icon to
// that manifest first, then import it here; see art/README.md.
import playerPlay from '../../../../datasrc/icons/tabler/player-play.svg?raw';
import map from '../../../../datasrc/icons/tabler/map.svg?raw';
import music from '../../../../datasrc/icons/tabler/music.svg?raw';
import palette from '../../../../datasrc/icons/tabler/palette.svg?raw';
import robot from '../../../../datasrc/icons/tabler/robot.svg?raw';
import shieldCheck from '../../../../datasrc/icons/tabler/shield-check.svg?raw';
import swords from '../../../../datasrc/icons/tabler/swords.svg?raw';
import trophy from '../../../../datasrc/icons/tabler/trophy.svg?raw';
import users from '../../../../datasrc/icons/tabler/users.svg?raw';
import x from '../../../../datasrc/icons/tabler/x.svg?raw';

const SOURCES = {
  'player-play': playerPlay,
  map,
  music,
  palette,
  robot,
  'shield-check': shieldCheck,
  swords,
  trophy,
  users,
  x,
};
export type IconName = keyof typeof SOURCES;
export const ICON_NAMES = Object.keys(SOURCES) as IconName[];

// Keep only the shapes; the wrapper's attributes come from <Icon> so every icon
// shares one stroke and follows the surrounding text colour.
const BODIES = Object.fromEntries(
  Object.entries(SOURCES).map(([name, svg]) => {
    const body = /<svg[^>]*>([\s\S]*)<\/svg>/.exec(svg)?.[1];
    if (!body) throw new Error(`Unreadable icon source: ${name}`);
    return [name, body.trim()];
  }),
) as Record<IconName, string>;

/** A theme-coloured interface icon; decorative unless given a label. */
export function Icon({
  name,
  size = 24,
  className,
  label,
}: {
  name: IconName;
  size?: number;
  className?: string;
  label?: string;
}) {
  return (
    <svg
      className={className ? `icon ${className}` : 'icon'}
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={2}
      strokeLinecap="round"
      strokeLinejoin="round"
      focusable="false"
      {...(label ? { role: 'img', 'aria-label': label } : { 'aria-hidden': true })}
      dangerouslySetInnerHTML={{ __html: BODIES[name] }}
    />
  );
}
