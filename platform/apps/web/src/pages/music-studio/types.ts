import type { MusicStudioRequest, MusicStudioSettings, MusicStudioThread } from '@glob2/protocol';
export const ROOT = '/api/v1/music-studio';
export interface Wallet {
  enabled: boolean;
  available: number;
  reserved: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
  usage: { id: string; kind: string; amount: string; created_at: string }[];
  activeRequest?: { id: string; threadId: string; status: string } | null;
}
export type {
  MusicStudioStageId as StageId,
  MusicStudioArtifact as Artifact,
  MusicStudioCheck as Check,
  MusicStudioProgress as Progress,
} from '@glob2/protocol';
export type Thread = MusicStudioThread;
export type Delivered = MusicStudioRequest & {
  release_id: string;
  input: MusicStudioRequest['input'] & { settings: MusicStudioSettings };
};
export const price = (p: Wallet['packs'][number]) =>
  new Intl.NumberFormat(undefined, { style: 'currency', currency: p.currency }).format(
    p.amount / 100,
  );
export function mergeThread(current: Thread | undefined, next: Thread): Thread {
  if (!current || current.id !== next.id) return next;
  function merge<T extends { id: string; created_at: string }>(previous: T[], latest: T[]) {
    return [...new Map([...previous, ...latest].map((v) => [v.id, v])).values()].sort(
      (a, b) => (a.created_at ?? '').localeCompare(b.created_at ?? '') || a.id.localeCompare(b.id),
    );
  }
  // Mutation responses, pagination and live snapshots may resolve out of order.
  // Add missing history from an older view, but never roll back committed state.
  const incomingIsOlder =
    /^\d+$/.test(current.cursor ?? '') &&
    /^\d+$/.test(next.cursor ?? '') &&
    BigInt(next.cursor ?? '0') < BigInt(current.cursor ?? '0');
  const older = incomingIsOlder ? next : current;
  const newer = incomingIsOlder ? current : next;
  return {
    ...newer,
    messages: merge(older.messages, newer.messages),
    requests: merge(older.requests, newer.requests),
    history: current.history ?? next.history,
  };
}
