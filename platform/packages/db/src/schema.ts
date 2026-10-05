// Typed view of the schema in migrations/*.sql, for Kysely. Keep it in step
// with the migrations: test/schema.test.ts writes and reads every table through
// these types and compares the column lists with information_schema.
import type { ColumnType, Generated, Insertable, Selectable, Updateable } from 'kysely';

/** Column with a database default: optional on insert. */
type Defaulted<T> = ColumnType<T, T | undefined, T>;
type Timestamp = ColumnType<Date, Date | string | undefined, Date | string>;
type RequiredTimestamp = ColumnType<Date, Date | string, Date | string>;
type NullableTimestamp = ColumnType<
  Date | null,
  Date | string | null | undefined,
  Date | string | null
>;
type Json<T> = ColumnType<T, string | T, string | T>;
type DefaultedJson<T> = ColumnType<T, string | T | undefined, string | T>;
type Nullable<T> = ColumnType<T | null, T | null | undefined, T | null>;
type NullableJson<T> = ColumnType<T | null, string | T | null | undefined, string | T | null>;

export type JsonValue =
  string | number | boolean | null | JsonValue[] | { [key: string]: JsonValue };

export interface AccountsTable {
  avatar_source: Defaulted<'automatic' | 'uploaded' | 'initials'>;
  avatar_key: Nullable<string>;
  avatar_revision: Defaulted<number>;
  gravatar_fingerprint: Nullable<string>;
  gravatar_checked_at: NullableTimestamp;
  gravatar_key: Nullable<string>;
  id: Generated<string>;
  kind: 'guest' | 'registered';
  display_name: string;
  role: Defaulted<'user' | 'moderator' | 'admin'>;
  status: Defaulted<'active' | 'banned' | 'deleted'>;
  muted_until: NullableTimestamp;
  created_at: Timestamp;
  updated_at: Timestamp;
  last_seen_at: NullableTimestamp;
  display_name_changed_at: NullableTimestamp;
  deleted_at: NullableTimestamp;
}

export interface IdentitiesTable {
  id: Generated<string>;
  account_id: string;
  provider: string;
  subject: string;
  email: Nullable<string>;
  password_hash: Nullable<string>;
  created_at: Timestamp;
  last_used_at: NullableTimestamp;
}

export interface DeviceCredentialsTable {
  id: Generated<string>;
  account_id: string;
  credential_hash: string;
  platform: 'desktop' | 'android' | 'ios' | 'browser';
  created_at: Timestamp;
  last_used_at: NullableTimestamp;
  revoked_at: NullableTimestamp;
}

export interface RefreshTokensTable {
  id: Generated<string>;
  account_id: string;
  family_id: string;
  token_hash: string;
  client_platform: Nullable<string>;
  issued_at: Timestamp;
  expires_at: RequiredTimestamp;
  rotated_at: NullableTimestamp;
  revoked_at: NullableTimestamp;
  replaced_by: Nullable<string>;
  grace_uses: Defaulted<number>;
}

export interface SigninAttemptsTable {
  id: Generated<string>;
  confirmation_code: string;
  provider: Nullable<string>;
  status: Defaulted<'pending' | 'completed' | 'failed' | 'cancelled' | 'expired'>;
  requesting_account_id: Nullable<string>;
  account_id: Nullable<string>;
  created_at: Timestamp;
  expires_at: RequiredTimestamp;
  completed_at: NullableTimestamp;
  resume_hash: Nullable<string>;
  mode: Defaulted<'link' | 'signin'>;
  client_platform: Defaulted<'desktop' | 'android' | 'ios' | 'browser'>;
  browser_binding_hash: Nullable<string>;
  conflict_account_id: Nullable<string>;
  failure_reason: Nullable<'expired' | 'denied' | 'cancelled' | 'conflict' | 'error'>;
  linked: Nullable<boolean>;
  delivered_at: NullableTimestamp;
  code_confirmed_at: NullableTimestamp;
  code_failures: Defaulted<number>;
  requesting_network_hash: Nullable<string>;
}

export interface WebSessionsTable {
  id: Generated<string>;
  account_id: string;
  token_hash: string;
  created_at: Timestamp;
  expires_at: RequiredTimestamp;
  last_used_at: NullableTimestamp;
  revoked_at: NullableTimestamp;
}

export interface AuthFlowsTable {
  id: Generated<string>;
  state_hash: string;
  provider: string;
  code_verifier: string;
  nonce: string;
  purpose: 'handoff' | 'web';
  attempt_id: Nullable<string>;
  browser_binding_hash: Nullable<string>;
  created_at: Timestamp;
  expires_at: RequiredTimestamp;
  consumed_at: NullableTimestamp;
}

export interface EntitlementsTable {
  id: Generated<string>;
  account_id: string;
  entitlement: string;
  source: string;
  granted_at: Timestamp;
  expires_at: NullableTimestamp;
  revoked_at: NullableTimestamp;
}

export interface AdminAuditLogTable {
  id: Generated<number>;
  actor_account_id: Nullable<string>;
  action: string;
  target_type: string;
  target_id: string;
  details: DefaultedJson<JsonValue>;
  created_at: Timestamp;
}

export interface BlobsTable {
  sha256: string;
  size: number;
  content_type: string;
  storage_key: string;
  visibility: Defaulted<'public' | 'private'>;
  owner_account_id: Nullable<string>;
  created_at: Timestamp;
}

export interface RelaysTable {
  id: string;
  public_url: string;
  region: string;
  build: string;
  turn_protocol: number;
  max_matches: number;
  active_matches: Defaulted<number>;
  connections: Defaulted<number>;
  cpu: Nullable<number>;
  draining: Defaulted<boolean>;
  registered_at: Timestamp;
  last_heartbeat_at: Timestamp;
}

export interface EngineAgentsTable {
  id: string;
  sim_version: string;
  kinds: string[];
  build: string;
  started_at: Timestamp;
  last_seen_at: Timestamp;
}

export interface EngineJobsTable {
  id: Generated<string>;
  kind: 'generate-map' | 'validate-map' | 'render-preview' | 'verify-match' | 'import-ai-map';
  sim_version: string;
  payload: Json<JsonValue>;
  status: Defaulted<'queued' | 'succeeded' | 'failed'>;
  result: NullableJson<JsonValue>;
  error: NullableJson<JsonValue>;
  agent_id: Nullable<string>;
  created_at: Timestamp;
  completed_at: NullableTimestamp;
  attempts: Defaulted<number>;
  max_attempts: Defaulted<number>;
  leased_by: Nullable<string>;
  lease_token_hash: Nullable<string>;
  lease_expires_at: NullableTimestamp;
  reported_at: NullableTimestamp;
  /** The match a verify-match job checks (from its payload). */
  match_id: Nullable<string>;
}

export interface AccountNameScrubsTable {
  account_id: string;
  match_id: string;
  created_at: Timestamp;
}

export interface RateLimitsTable {
  bucket: string;
  key: string;
  window_start: RequiredTimestamp;
  count: Defaulted<number>;
  previous_count: Defaulted<number>;
}

export interface MapsTable {
  authoring: NullableJson<JsonValue>;
  id: Generated<string>;
  owner_account_id: string;
  title: string;
  description: Defaulted<string>;
  visibility: 'public' | 'unlisted' | 'private';
  hidden: Defaulted<boolean>;
  hidden_reason: Nullable<string>;
  play_count: Defaulted<number>;
  download_count: Defaulted<number>;
  created_at: Timestamp;
  updated_at: Timestamp;
  // 0007 map catalog
  made_with: Defaulted<'hand' | 'generator'>;
  generator: NullableJson<JsonValue>;
  like_count: Defaulted<number>;
  latest_version_id: Nullable<string>;
  hidden_at: NullableTimestamp;
  hidden_by_account_id: Nullable<string>;
}

export interface MapVersionsTable {
  id: Generated<string>;
  map_id: string;
  hash: string;
  size: number;
  width: Nullable<number>;
  height: Nullable<number>;
  team_count: Nullable<number>;
  min_version_minor: Nullable<number>;
  preview_hash: Nullable<string>;
  validation: Defaulted<'pending' | 'valid' | 'invalid'>;
  validation_error: Nullable<string>;
  created_at: Timestamp;
  // 0007 map catalog
  sim_version: Nullable<string>;
  validate_job_id: Nullable<string>;
  preview_job_id: Nullable<string>;
  preview_status: Defaulted<'pending' | 'ready' | 'failed'>;
  preview_width: Nullable<number>;
  preview_height: Nullable<number>;
  file_title: Nullable<string>;
  uploader_account_id: Nullable<string>;
  notes: Defaulted<string>;
}

export interface MapLikesTable {
  map_id: string;
  account_id: string;
  created_at: Timestamp;
}

export interface MapReportsTable {
  id: Generated<string>;
  map_id: string;
  reporter_account_id: string;
  reason: 'broken' | 'offensive' | 'copyright' | 'other';
  details: Defaulted<string>;
  status: Defaulted<'open' | 'resolved' | 'dismissed'>;
  resolved_by_account_id: Nullable<string>;
  created_at: Timestamp;
  resolved_at: NullableTimestamp;
  resolution_note: Nullable<string>;
}

export interface MapDownloadsTable {
  map_id: string;
  downloader: string;
  day: Defaulted<string>;
}

export interface RoomsTable {
  id: Generated<string>;
  code: string;
  name: string;
  visibility: 'public' | 'link';
  status: Defaulted<'open' | 'starting' | 'in_match' | 'closed'>;
  host_account_id: string;
  sim_version: string;
  settings: Json<JsonValue>;
  revision: Defaulted<number>;
  match_id: Nullable<string>;
  created_at: Timestamp;
  updated_at: Timestamp;
  closed_at: NullableTimestamp;
  starting_since: NullableTimestamp;
  notice: Nullable<string>;
}

export interface RoomKicksTable {
  room_id: string;
  account_id: string;
  kicked_by_account_id: Nullable<string>;
  until: RequiredTimestamp;
  created_at: Timestamp;
}

export interface RoomMembersTable {
  room_id: string;
  account_id: string;
  connected: Defaulted<boolean>;
  joined_at: Timestamp;
  last_seen_at: Timestamp;
  region_rtts: DefaultedJson<JsonValue>;
}

export interface RoomSeatsTable {
  room_id: string;
  seat: number;
  team: number;
  occupant: Defaulted<'open' | 'human' | 'ai'>;
  account_id: Nullable<string>;
  ai_id: Nullable<string>;
  ai_name: Nullable<string>;
  ready: Defaulted<boolean>;
  locked: Defaulted<boolean>;
}

export interface RoomChatMessagesTable {
  id: Generated<string>;
  room_id: string;
  account_id: string;
  text: string;
  sent_at: Timestamp;
}

export type MatchVerification =
  'pending' | 'verified' | 'diverged' | 'unverifiable' | 'not_applicable' | 'failed';

export interface MatchesTable {
  skins_frozen_at: NullableTimestamp;
  id: Generated<string>;
  sim_version: string;
  origin: 'room' | 'queue';
  room_id: Nullable<string>;
  queue_id: Nullable<string>;
  rated: Defaulted<boolean>;
  status: Defaulted<'starting' | 'running' | 'ended' | 'cancelled'>;
  verification: Defaulted<MatchVerification>;
  setup: Json<JsonValue>;
  seed: number;
  map_hash: string;
  relay_id: Nullable<string>;
  end_reason: Nullable<'completed' | 'abandoned' | 'aborted'>;
  final_tick: Nullable<number>;
  desync_flagged: Defaulted<boolean>;
  created_at: Timestamp;
  started_at: NullableTimestamp;
  ended_at: NullableTimestamp;
  rating_status: Defaulted<'pending' | 'applied' | 'unchanged' | 'not_rated'>;
  rating_note: Nullable<string>;
  ratings_applied_at: NullableTimestamp;
  proposal_id: Nullable<string>;
  relay_assigned_at: NullableTimestamp;
  relay_attempts: Defaulted<number>;
  end_report: NullableJson<JsonValue>;
  relay_seen_at: NullableTimestamp;
}

export interface MapUploadsTable {
  id: Generated<string>;
  owner_account_id: string;
  blob_sha256: string;
  format: 'map' | 'save';
  sim_version: string;
  file_name: Nullable<string>;
  status: Defaulted<'pending' | 'valid' | 'invalid'>;
  job_id: Nullable<string>;
  width: Nullable<number>;
  height: Nullable<number>;
  team_count: Nullable<number>;
  version_minor: Nullable<number>;
  title: Nullable<string>;
  players: NullableJson<JsonValue>;
  failure: Nullable<string>;
  created_at: Timestamp;
  completed_at: NullableTimestamp;
}

export interface GeneratedMapsTable {
  descriptor_hash: string;
  sim_version: string;
  descriptor: Json<JsonValue>;
  status: Defaulted<'pending' | 'ready' | 'failed'>;
  job_id: Nullable<string>;
  map_hash: Nullable<string>;
  width: Nullable<number>;
  height: Nullable<number>;
  team_count: Nullable<number>;
  failure: Nullable<string>;
  created_at: Timestamp;
  completed_at: NullableTimestamp;
}

export interface RatingEntitiesTable {
  id: Generated<string>;
  kind: 'account' | 'ai';
  account_id: Nullable<string>;
  ai_id: Nullable<string>;
  ai_sim_version: Nullable<string>;
  created_at: Timestamp;
}

export interface RatingsTable {
  entity_id: string;
  ladder: string;
  mu: number;
  sigma: number;
  ordinal: ColumnType<number, never, never>;
  games: Defaulted<number>;
  wins: Defaulted<number>;
  last_match_id: Nullable<string>;
  updated_at: Timestamp;
  seed_source: Nullable<string>;
}

export interface RatingHistoryTable {
  match_id: string;
  entity_id: string;
  ladder: string;
  result: 'won' | 'lost';
  mu_before: number;
  sigma_before: number;
  mu_after: number;
  sigma_after: number;
  display_before: number;
  display_after: number;
  created_at: Timestamp;
}

export type Outcome = 'won' | 'lost' | 'draw' | 'unresolved' | 'abandoned';

export interface MatchParticipantsTable {
  match_id: string;
  seat: number;
  team: number;
  kind: 'human' | 'ai';
  account_id: Nullable<string>;
  ai_id: Nullable<string>;
  rating_entity_id: Nullable<string>;
  display_name: string;
  outcome: Nullable<Outcome>;
  disconnects: Defaulted<number>;
  quit_tick: Nullable<number>;
  /** The relay's RelayNetworkSummary seat entry (0009); null without one. */
  network: NullableJson<JsonValue>;
  rating_before: Nullable<number>;
  rating_after: Nullable<number>;
}

export interface MatchTeamStatsTable {
  match_id: string;
  team: number;
  outcome: Outcome;
  prestige: Defaulted<number>;
  eliminated_tick: Nullable<number>;
  statistics: DefaultedJson<JsonValue>;
  timeline: DefaultedJson<JsonValue>;
}

export interface MatchArtifactsTable {
  match_id: string;
  kind: 'record' | 'replay' | 'result';
  blob_sha256: string;
  created_at: Timestamp;
}

export interface QueueTicketsTable {
  id: Generated<string>;
  queue_id: string;
  account_id: string;
  sim_version: string;
  region_rtts: DefaultedJson<JsonValue>;
  rating_mu: Nullable<number>;
  rating_sigma: Nullable<number>;
  status: Defaulted<QueueTicketStatus>;
  match_id: Nullable<string>;
  created_at: Timestamp;
  updated_at: Timestamp;
  allow_ai_opponent: Defaulted<boolean>;
  proposal_id: Nullable<string>;
  /** The search this ticket belongs to: tickets of one search enter different queues together. */
  search_id: Generated<string>;
}

export type QueueTicketStatus =
  'waiting' | 'proposed' | 'matched' | 'cancelled' | 'declined' | 'expired';

export type ProposalStatus = 'pending' | 'starting' | 'started' | 'cancelled' | 'failed';

export interface MatchProposalsTable {
  id: Generated<string>;
  queue_id: string;
  sim_version: string;
  region: Nullable<string>;
  rated: boolean;
  backfilled: Defaulted<boolean>;
  status: ProposalStatus;
  map: Json<JsonValue>;
  expires_at: NullableTimestamp;
  match_id: Nullable<string>;
  start_attempts: Defaulted<number>;
  failure: Nullable<string>;
  created_at: Timestamp;
  resolved_at: NullableTimestamp;
}

export type ProposalResponse = 'pending' | 'accepted' | 'declined' | 'timeout' | 'not_required';

export interface MatchProposalSeatsTable {
  proposal_id: string;
  slot: number;
  side: number;
  kind: 'human' | 'ai';
  ticket_id: Nullable<string>;
  account_id: Nullable<string>;
  ai_id: Nullable<string>;
  rating_entity_id: Nullable<string>;
  mu: number;
  sigma: number;
  response: Defaulted<ProposalResponse>;
  responded_at: NullableTimestamp;
}

export interface QueueCooldownsTable {
  account_id: string;
  until: RequiredTimestamp;
  reason: 'declined' | 'timeout';
  created_at: Timestamp;
}

export interface WarmMapsTable {
  id: Generated<string>;
  queue_id: string;
  sim_version: string;
  entry_key: string;
  /** The generated map (generated_maps, with sim_version); null only on rows taken before 0019. */
  descriptor_hash: Nullable<string>;
  match_id: Nullable<string>;
  created_at: Timestamp;
  taken_at: NullableTimestamp;
}

// ------------------------------------------------- read-only views (0004)

/** Read-only column of a view. */
type View<T> = ColumnType<T, never, never>;

export interface MatchResultsView {
  match_id: View<string>;
  origin: View<'room' | 'queue'>;
  queue_id: View<string | null>;
  rated: View<boolean>;
  sim_version: View<string>;
  map_hash: View<string>;
  generator_id: View<string | null>;
  final_tick: View<number | null>;
  ended_at: View<Date>;
  seat: View<number>;
  team: View<number>;
  kind: View<'human' | 'ai'>;
  account_id: View<string | null>;
  ai_id: View<string | null>;
  rating_entity_id: View<string | null>;
  outcome: View<Outcome | null>;
  won: View<boolean | null>;
}

export interface RecentWinRatesView {
  account_id: View<string | null>;
  ai_id: View<string | null>;
  ai_sim_version: View<string | null>;
  dimension: View<'queue' | 'map' | 'generator'>;
  key: View<string>;
  games: View<number>;
  wins: View<number>;
  win_rate: View<number>;
  last_played_at: View<Date>;
}

export interface RecentGameLengthsView {
  dimension: View<'queue' | 'generator'>;
  key: View<string>;
  games: View<number>;
  mean_ticks: View<number>;
  median_ticks: View<number>;
  p90_ticks: View<number>;
  max_ticks: View<number>;
}

export interface TeamTimelineView {
  match_id: View<string>;
  team: View<number>;
  tick: View<number>;
  units: View<number>;
  buildings: View<number>;
  prestige: View<number>;
  hp: View<number>;
  attack: View<number>;
  defense: View<number>;
}

export interface AccountEconomyCurvesView {
  account_id: View<string>;
  match_id: View<string>;
  queue_id: View<string | null>;
  ended_at: View<Date>;
  tick: View<number>;
  units: View<number>;
  buildings: View<number>;
  prestige: View<number>;
  average_units: View<number>;
  average_buildings: View<number>;
  average_prestige: View<number>;
  games_at_tick: View<number>;
}

export interface LeaderLeasesTable {
  name: string;
  epoch: number;
  holder: string;
  acquired_at: Timestamp;
  renewed_at: Timestamp;
}

export interface ApiReplicasTable {
  id: string;
  started_at: Timestamp;
  heartbeat_at: Timestamp;
}

export interface RealtimePresenceTable {
  account_id: string;
  replica_id: string;
  since: Timestamp;
}

export interface NotificationPayloadsTable {
  id: Generated<string>;
  channel: string;
  payload: Json<JsonValue>;
  created_at: Timestamp;
}

export interface ColonySkinsTable {
  id: Generated<string>;
  owner_account_id: Nullable<string>;
  kind: 'preset' | 'custom';
  name: string;
  entitlement: string;
  disabled_at: NullableTimestamp;
  created_at: Timestamp;
}

export interface ColonySkinVersionsTable {
  id: Generated<string>;
  skin_id: string;
  texture_sha256: string;
  material_sha256: string;
  layout: 'colony-v2';
  building_color: number;
  /** Validated against the protocol's SWARM_MESHES by the API. */
  swarm_mesh: Defaulted<string>;
  manifest_sha256: string;
  created_at: Timestamp;
}

export interface ColonySkinEquipmentTable {
  building_color: Nullable<number>;
  account_id: string;
  version_id: string;
  updated_at: Timestamp;
}

export interface MatchColonySkinsTable {
  building_color: number;
  match_id: string;
  team_index: number;
  account_id: string;
  version_id: string;
  assertion: string;
  created_at: Timestamp;
}

export interface SkinPurchasesTable {
  id: Generated<string>;
  account_id: string;
  request_id: string;
  sku: 'designer' | 'stripes' | 'spots';
  entitlement: string;
  price_id: string;
  checkout_id: Nullable<string>;
  payment_intent_id: Nullable<string>;
  status: Defaulted<'pending' | 'paid' | 'refunded' | 'disputed' | 'failed'>;
  entitlement_id: Nullable<string>;
  created_at: Timestamp;
  updated_at: Timestamp;
  reconcile_after: Timestamp;
  recovery_cursor: Nullable<string>;
}
export interface SkinPaymentEventsTable {
  id: string;
  event_type: string;
  purchase_id: Nullable<string>;
  processed_at: Timestamp;
}
export interface ColonySkinDraftsTable {
  skin_id: Nullable<string>;
  account_id: string;
  revision: string;
  name: string;
  building_color: number;
  swarm_mesh: Defaulted<string>;
  image: Buffer;
  material: Buffer;
  updated_at: Timestamp;
}

export interface ColonySkinReportsTable {
  id: Generated<string>;
  version_id: string;
  reporter_account_id: string;
  reason: string;
  created_at: Timestamp;
  resolution: Nullable<'dismissed' | 'disabled'>;
  resolved_at: NullableTimestamp;
  resolved_by_account_id: Nullable<string>;
  resolution_reason: Nullable<string>;
}
// The shared database connection parses bounded bigint values as numbers.
export interface HiveWalletsTable {
  account_id: string;
  balance: Defaulted<number>;
  reserved: Defaulted<number>;
}
export interface HiveLedgerTable {
  id: string;
  account_id: string;
  amount: number;
  kind: string;
  details: DefaultedJson<JsonValue>;
  created_at: Timestamp;
}
export interface HiveCallsTable {
  id: string;
  account_id: string;
  reserved: number;
  status: string;
  charged: Nullable<number>;
  rate: Json<JsonValue>;
  usage: NullableJson<JsonValue>;
  created_at: Timestamp;
}
export interface HiveSessionsTable {
  id: Generated<string>;
  account_id: string;
  match_id: string;
  seat: number;
  team: number;
  client_id: Nullable<string>;
  lease: Nullable<string>;
  lease_until: NullableTimestamp;
  tick: Defaulted<number>;
  generation: Defaulted<number>;
  supervision: Defaulted<boolean>;
  pending_run: Defaulted<boolean>;
  run_id: Nullable<string>;
  run_until: NullableTimestamp;
  last_wake_tick: Nullable<number>;
  wake_window: NullableTimestamp;
  wake_count: Defaulted<number>;
  created_at: Timestamp;
}
export interface HiveEventsTable {
  id: Generated<number>;
  session_id: string;
  dedup: string;
  kind: string;
  body: Json<JsonValue>;
  created_at: Timestamp;
}
export interface HiveOperationsTable {
  supervised: Defaulted<boolean>;
  id: string;
  session_id: string;
  generation: number;
  lease: Nullable<string>;
  status: string;
  request: Json<JsonValue>;
  result: NullableJson<JsonValue>;
  created_at: Timestamp;
}
export interface HiveProgramsTable {
  supervised: Defaulted<boolean>;
  session_id: string;
  id: string;
  revision: number;
  definition: Json<JsonValue>;
  status: string;
}
export interface HivePurchasesTable {
  id: string;
  account_id: string;
  checkout_id: Nullable<string>;
  payment_id: Nullable<string>;
  pack: Json<JsonValue>;
  paid: Defaulted<boolean>;
  reversed: Defaulted<number>;
  created_at: Timestamp;
}

export interface StudioEventsTable {
  thread_id: string;
  cursor: number;
  request_id: Nullable<string>;
  dedup: string;
  type: string;
  payload: Json<JsonValue>;
  created_at: Timestamp;
}
export interface StudioProviderUsageTable {
  day: string;
  calls: number;
}
export interface StudioArtifactsTable {
  id: Generated<string>;
  thread_id: string;
  request_id: string;
  stage: string;
  kind: string;
  label: string;
  hash: string;
  width: Nullable<number>;
  height: Nullable<number>;
  created_at: Timestamp;
}
export interface StudioThreadsTable {
  event_cursor: Defaulted<number>;
  id: Generated<string>;
  account_id: string;
  title: string;
  brief: Defaulted<string>;
  created_at: Timestamp;
  updated_at: Timestamp;
}
export interface StudioMessagesTable {
  id: string;
  thread_id: string;
  role: 'user' | 'assistant';
  text: string;
  created_at: Timestamp;
}
export interface StudioRequestsTable {
  id: string;
  thread_id: string;
  account_id: string;
  kind: 'chat' | 'generate';
  status: Defaulted<string>;
  input: Json<JsonValue>;
  checkpoints: DefaultedJson<JsonValue>;
  lease_until: NullableTimestamp;
  lease: Nullable<string>;
  map_id: Nullable<string>;
  map_hash: Nullable<string>;
  error: Nullable<string>;
  charged: Defaulted<boolean>;
  created_at: Timestamp;
  completed_at: NullableTimestamp;
}
export interface StudioAttemptsTable {
  id: string;
  request_id: string;
  stage: string;
  model: string;
  status: string;
  input: Json<JsonValue>;
  output: NullableJson<JsonValue>;
  created_at: Timestamp;
}

export interface MusicReleasesTable {
  id: string;
  owner_id: string;
  metadata: Json<import('@glob2/protocol').MusicMetadata>;
  status: Defaulted<import('@glob2/protocol').MusicRelease['status']>;
  sources: DefaultedJson<Record<string, string>>;
  inspection: NullableJson<NonNullable<import('@glob2/protocol').MusicRelease['inspection']>>;
  result: NullableJson<import('@glob2/protocol').MusicTrack[] | Record<string, JsonValue>>;
  options: NullableJson<import('@glob2/protocol').MusicConvert>;
  error: Nullable<string>;
  hidden: Defaulted<boolean>;
  downloads: Defaulted<number>;
  created_at: Timestamp;
  updated_at: Timestamp;
}
export interface Database {
  music_releases: MusicReleasesTable;
  music_assets: { release_id: string; kind: string; sha256: string };
  music_likes: { release_id: string; account_id: string };
  music_reports: {
    id: Generated<string>;
    release_id: string;
    account_id: string;
    reason: string;
    resolved: Defaulted<boolean>;
    created_at: Timestamp;
  };

  colony_skin_reports: ColonySkinReportsTable;
  colony_skin_drafts: ColonySkinDraftsTable;
  skin_purchases: SkinPurchasesTable;
  skin_payment_events: SkinPaymentEventsTable;
  colony_skins: ColonySkinsTable;
  colony_skin_versions: ColonySkinVersionsTable;
  colony_skin_equipment: ColonySkinEquipmentTable;
  match_colony_skins: MatchColonySkinsTable;

  studio_events: StudioEventsTable;
  studio_provider_usage: StudioProviderUsageTable;
  studio_artifacts: StudioArtifactsTable;
  studio_threads: StudioThreadsTable;
  studio_messages: StudioMessagesTable;
  studio_requests: StudioRequestsTable;
  studio_attempts: StudioAttemptsTable;
  map_wallets: HiveWalletsTable;
  map_ledger: HiveLedgerTable;
  map_calls: HiveCallsTable;
  map_purchases: HivePurchasesTable;
  hive_wallets: HiveWalletsTable;
  hive_ledger: HiveLedgerTable;
  hive_calls: HiveCallsTable;
  hive_sessions: HiveSessionsTable;
  hive_events: HiveEventsTable;
  hive_operations: HiveOperationsTable;
  hive_programs: HiveProgramsTable;
  hive_purchases: HivePurchasesTable;
  accounts: AccountsTable;
  identities: IdentitiesTable;
  device_credentials: DeviceCredentialsTable;
  refresh_tokens: RefreshTokensTable;
  signin_attempts: SigninAttemptsTable;
  web_sessions: WebSessionsTable;
  auth_flows: AuthFlowsTable;
  entitlements: EntitlementsTable;
  admin_audit_log: AdminAuditLogTable;
  blobs: BlobsTable;
  relays: RelaysTable;
  engine_agents: EngineAgentsTable;
  engine_jobs: EngineJobsTable;
  maps: MapsTable;
  map_versions: MapVersionsTable;
  map_likes: MapLikesTable;
  map_reports: MapReportsTable;
  rooms: RoomsTable;
  room_members: RoomMembersTable;
  room_kicks: RoomKicksTable;
  map_downloads: MapDownloadsTable;
  room_seats: RoomSeatsTable;
  room_chat_messages: RoomChatMessagesTable;
  matches: MatchesTable;
  rating_entities: RatingEntitiesTable;
  ratings: RatingsTable;
  match_participants: MatchParticipantsTable;
  match_team_stats: MatchTeamStatsTable;
  match_artifacts: MatchArtifactsTable;
  queue_tickets: QueueTicketsTable;
  rating_history: RatingHistoryTable;
  match_proposals: MatchProposalsTable;
  match_proposal_seats: MatchProposalSeatsTable;
  queue_cooldowns: QueueCooldownsTable;
  map_uploads: MapUploadsTable;
  generated_maps: GeneratedMapsTable;
  warm_maps: WarmMapsTable;
  rate_limits: RateLimitsTable;
  account_name_scrubs: AccountNameScrubsTable;
  leader_leases: LeaderLeasesTable;
  notification_payloads: NotificationPayloadsTable;
  api_replicas: ApiReplicasTable;
  realtime_presence: RealtimePresenceTable;
  match_results_view: MatchResultsView;
  recent_win_rates_view: RecentWinRatesView;
  recent_game_lengths_view: RecentGameLengthsView;
  team_timeline_view: TeamTimelineView;
  account_economy_curves_view: AccountEconomyCurvesView;
}

export type Account = Selectable<AccountsTable>;
export type NewAccount = Insertable<AccountsTable>;
export type AccountUpdate = Updateable<AccountsTable>;
export type Match = Selectable<MatchesTable>;
export type NewMatch = Insertable<MatchesTable>;
export type Room = Selectable<RoomsTable>;
export type WarmMap = Selectable<WarmMapsTable>;
