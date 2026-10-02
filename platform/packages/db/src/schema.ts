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
  kind: 'generate-map' | 'validate-map' | 'render-preview' | 'verify-match';
  sim_version: string;
  payload: Json<JsonValue>;
  status: Defaulted<'queued' | 'succeeded' | 'failed'>;
  result: NullableJson<JsonValue>;
  error: NullableJson<JsonValue>;
  agent_id: Nullable<string>;
  created_at: Timestamp;
  completed_at: NullableTimestamp;
}

export interface MapsTable {
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

export interface MatchesTable {
  id: Generated<string>;
  sim_version: string;
  origin: 'room' | 'queue';
  room_id: Nullable<string>;
  queue_id: Nullable<string>;
  rated: Defaulted<boolean>;
  status: Defaulted<'starting' | 'running' | 'ended' | 'cancelled'>;
  verification: Defaulted<'pending' | 'verified' | 'diverged' | 'unverifiable' | 'not_applicable'>;
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

export type Outcome = 'won' | 'lost' | 'unresolved' | 'abandoned';

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
  generator: Json<JsonValue>;
  status: Defaulted<'generating' | 'ready' | 'taken' | 'failed'>;
  job_id: Nullable<string>;
  map_hash: Nullable<string>;
  map_facts: NullableJson<JsonValue>;
  failure: Nullable<string>;
  match_id: Nullable<string>;
  created_at: Timestamp;
  ready_at: NullableTimestamp;
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

export interface Database {
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
