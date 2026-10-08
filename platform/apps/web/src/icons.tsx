import list_details from '../../../../datasrc/icons/tabler/list-details.svg?raw';
import message from '../../../../datasrc/icons/tabler/message.svg?raw';
import send from '../../../../datasrc/icons/tabler/send.svg?raw';
import plus from '../../../../datasrc/icons/tabler/plus.svg?raw';
import pencil from '../../../../datasrc/icons/tabler/pencil.svg?raw';
import eye from '../../../../datasrc/icons/tabler/eye.svg?raw';
import restore from '../../../../datasrc/icons/tabler/restore.svg?raw';
import refresh from '../../../../datasrc/icons/tabler/refresh.svg?raw';
import download from '../../../../datasrc/icons/tabler/download.svg?raw';
import upload from '../../../../datasrc/icons/tabler/upload.svg?raw';
import share from '../../../../datasrc/icons/tabler/share.svg?raw';
import settings from '../../../../datasrc/icons/tabler/settings.svg?raw';
import coins from '../../../../datasrc/icons/tabler/coins.svg?raw';
import check from '../../../../datasrc/icons/tabler/check.svg?raw';
import alert_triangle from '../../../../datasrc/icons/tabler/alert-triangle.svg?raw';
import wifi_off from '../../../../datasrc/icons/tabler/wifi-off.svg?raw';
import loader_2 from '../../../../datasrc/icons/tabler/loader-2.svg?raw';
import adjustments_horizontal from '../../../../datasrc/icons/tabler/adjustments-horizontal.svg?raw';
import chevron_down from '../../../../datasrc/icons/tabler/chevron-down.svg?raw';
import chevron_right from '../../../../datasrc/icons/tabler/chevron-right.svg?raw';
import folder_open from '../../../../datasrc/icons/tabler/folder-open.svg?raw';
import flask from '../../../../datasrc/icons/tabler/flask.svg?raw';
import copy from '../../../../datasrc/icons/tabler/copy.svg?raw';
import info_circle from '../../../../datasrc/icons/tabler/info-circle.svg?raw';
import building from '../../../../datasrc/icons/tabler/building.svg?raw';
import mountain from '../../../../datasrc/icons/tabler/mountain.svg?raw';
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
  'list-details': list_details,
  message: message,
  send: send,
  plus: plus,
  pencil: pencil,
  eye: eye,
  restore: restore,
  refresh: refresh,
  download: download,
  upload: upload,
  share: share,
  settings: settings,
  coins: coins,
  check: check,
  'alert-triangle': alert_triangle,
  'wifi-off': wifi_off,
  'loader-2': loader_2,
  'adjustments-horizontal': adjustments_horizontal,
  'chevron-down': chevron_down,
  'chevron-right': chevron_right,
  'folder-open': folder_open,
  flask: flask,
  copy: copy,
  'info-circle': info_circle,
  building: building,
  mountain: mountain,

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
