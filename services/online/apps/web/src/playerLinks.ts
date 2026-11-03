import type { DirectoryPlayer, SimVersion } from '@glob2/protocol';
import { versionKey } from './format.ts';

export function playerHref(player: DirectoryPlayer) {
  return player.kind === 'account'
    ? `/players/${player.account.id}`
    : aiHref(player.ai, player.simVersion ? versionKey(player.simVersion) : undefined);
}
export function aiHref(ai: string, simVersion?: string) {
  return `/players/ai/${encodeURIComponent(ai)}${simVersion ? `?simVersion=${encodeURIComponent(simVersion)}` : ''}`;
}

export function versionLabel(version: SimVersion) {
  return `${version.versionMinor} / ${version.netProtocol} · ${version.dataHash.slice(0, 8)}`;
}
