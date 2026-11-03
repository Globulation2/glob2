import { getLocale } from '../../messages.ts';
import { mergeStudioThread } from '../../components/studio/history.ts';
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
  new Intl.NumberFormat(getLocale(), { style: 'currency', currency: p.currency }).format(
    p.amount / 100,
  );
export const mergeThread = mergeStudioThread<Thread>;
