import { mergeStudioThread } from '../../components/studio/history.ts';
import type {
  StudioRequest,
  StudioSettings,
  StudioThread,
  StudioStageProgress as Stage,
} from '@glob2/protocol';
export const ROOT = '/api/v1/map-studio';
export interface Wallet {
  enabled: boolean;
  available: number;
  reserved: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
  usage: { id: string; kind: string; amount: string; created_at: string }[];
  activeRequest?: { id: string; threadId: string; status: string } | null;
}
export type {
  StudioStageId as StageId,
  StudioStageProgress as Stage,
  StudioArtifact as Artifact,
  StudioCheck as Check,
  StudioProgress as Progress,
} from '@glob2/protocol';
export type Thread = StudioThread;
export type Delivered = StudioRequest & {
  map_id: string;
  map_hash: string;
  input: StudioRequest['input'] & { settings: StudioSettings };
};
export const STAGES: Stage[] = [
  { id: 'prepare', label: 'Prepare the design', status: 'pending' },
  { id: 'terrain', label: 'Shape the terrain', status: 'pending' },
  { id: 'build', label: 'Build the playable map', status: 'pending' },
  { id: 'checks', label: 'Check the essentials', status: 'pending' },
  { id: 'ready', label: 'Ready', status: 'pending' },
];
export const preview = (v: Delivered) =>
  `/api/v1/maps/${v.map_id}/versions/${v.map_hash}/preview.webp`;
export const price = (p: Wallet['packs'][number]) =>
  new Intl.NumberFormat(undefined, { style: 'currency', currency: p.currency }).format(
    p.amount / 100,
  );
export const mergeThread = mergeStudioThread<Thread>;
