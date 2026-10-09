/** Contracts shared by private coding workspaces. Each domain supplies its run shape. */
export interface StudioRevision {
  revision: number;
  source: string;
  hash: string;
  reason: string;
  created_at: string;
}
export interface CodingStudioRequest {
  id: string;
  base_revision: number;
  prompt: string;
  diagnostics: string;
  budget: number;
  status: string;
  response: string;
  error: string | null;
  charged: number | null;
}
export interface StudioProject {
  id: string;
  title: string;
  revision: number;
  updated_at: string;
}
export interface StudioDetail<Run = unknown> extends StudioProject {
  current: StudioRevision;
  revisions: Omit<StudioRevision, 'source'>[];
  requests: CodingStudioRequest[];
  cursor: string;
  runs: Run[];
}
export interface StudioAccount {
  enabled: boolean;
  model: string;
  maxRequestCredits: number;
  rate: { input: number; cachedInput: number; output: number } | null;
  balance: number;
  reserved: number;
  available: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
}
