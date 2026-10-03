// Colours from the game: team colours (src/team/Team.cpp setCorrectColor:
// hue = team * 360 / teamCount, HSV saturation 0.8, value 0.9), and chart
// inks derived from them that keep at least 3:1 contrast on the chart
// background of either theme (WCAG 1.4.11 for graphical objects).
import type { Theme } from './theme.tsx';

type Rgb = [number, number, number];

export const CHART_BACKGROUND: Record<Theme, string> = { light: '#fbfbf3', dark: '#2b1c42' };

function hsvToRgb(h: number, s: number, v: number): Rgb {
  const f = (n: number) => {
    const k = (n + h / 60) % 6;
    return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
  };
  return [f(5), f(3), f(1)];
}

function hslToRgb(h: number, s: number, l: number): Rgb {
  const a = s * Math.min(l, 1 - l);
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
  };
  return [f(0), f(8), f(4)];
}

function toHex([r, g, b]: Rgb): string {
  const c = (x: number) =>
    Math.round(Math.max(0, Math.min(1, x)) * 255)
      .toString(16)
      .padStart(2, '0');
  return `#${c(r)}${c(g)}${c(b)}`;
}

function parseHex(hex: string): Rgb {
  const n = Number.parseInt(hex.slice(1), 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

function luminance([r, g, b]: Rgb): number {
  const lin = (c: number) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

export function contrast(a: string, b: string): number {
  const la = luminance(parseHex(a));
  const lb = luminance(parseHex(b));
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

/** The hue (degrees) of team `team` on a map of `teamCount` teams, as the engine assigns it. */
export function teamHue(team: number, teamCount: number): number {
  const count = Math.max(1, teamCount, team + 1);
  return ((team * 360) / count) % 360;
}

/** The exact in-game colour of a team. */
export function gameTeamColor(team: number, teamCount: number): string {
  return toHex(hsvToRgb(teamHue(team, teamCount), 0.8, 0.9));
}

/** A colour of hue `hue` with at least `ratio` contrast on `background`. */
export function readableHue(hue: number, background: string, ratio = 3, saturation = 0.85) {
  const dark = luminance(parseHex(background)) < 0.2;
  // Walk lightness away from the background until the contrast is enough.
  for (let step = 0; step <= 40; step++) {
    const l = dark ? 0.55 + step * 0.01 : 0.5 - step * 0.01;
    const hex = toHex(hslToRgb(hue, saturation, l));
    if (contrast(hex, background) >= ratio) return hex;
  }
  return dark ? '#ffffff' : '#000000';
}

const inkCache = new Map<string, string>();

/** Chart/legend ink for a team: the team's hue, readable in the current theme. */
export function teamInk(team: number, teamCount: number, theme: Theme): string {
  const key = `${team}/${teamCount}/${theme}`;
  let ink = inkCache.get(key);
  if (!ink) {
    ink = readableHue(teamHue(team, teamCount), CHART_BACKGROUND[theme], 3.2);
    inkCache.set(key, ink);
  }
  return ink;
}

/** Non-team series (rating per ladder, "this match" against "average"). */
export function seriesInk(index: number, theme: Theme): string {
  // Sky blue, wheat gold, swarm green, plum: picked from the game's world.
  const hues = [212, 38, 150, 285];
  return readableHue(hues[index % hues.length] ?? 212, CHART_BACKGROUND[theme], 3.4, 0.7);
}

/** A stable glob colour for a player's avatar, dark enough for white initials (4.5:1). */
export function avatarColor(seed: string): string {
  let hash = 0;
  for (const ch of seed) hash = (hash * 31 + ch.charCodeAt(0)) | 0;
  const hue = Math.abs(hash) % 360;
  for (let l = 0.42; l > 0.1; l -= 0.02) {
    const hex = toHex(hslToRgb(hue, 0.55, l));
    if (contrast(hex, '#ffffff') >= 4.5) return hex;
  }
  return '#2f4f3a';
}
