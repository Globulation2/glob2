import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { STANDARD_RULES } from '@glob2/protocol';
import {
  SqlFileMigrationProvider,
  createMigrator,
  migrateToLatest,
  type Database,
} from '../src/index.ts';
import { createTestDatabase, type TestDatabase } from './support.ts';

type ColumnLists = { [T in keyof Database]: readonly (keyof Database[T] & string)[] };

/** Every column of every table, as typed in src/schema.ts. */
const typedColumns: ColumnLists = {
  ais: [
    'id',
    'owner_account_id',
    'name',
    'description',
    'tags',
    'visibility',
    'hidden',
    'hidden_reason',
    'created_at',
    'updated_at',
  ],
  ai_versions: ['id', 'ai_id', 'hash', 'label', 'notes', 'profile', 'created_at'],
  ai_validations: [
    'id',
    'hash',
    'sim_version',
    'suite',
    'job_id',
    'status',
    'report',
    'error',
    'created_at',
  ],
  ai_uploads: [
    'id',
    'owner_account_id',
    'validation_id',
    'expires_at',
    'published_ai_id',
    'published_version_id',
  ],
  ai_likes: ['ai_id', 'account_id'],
  ai_favourites: ['ai_id', 'account_id'],
  ai_downloads: ['version_id', 'downloader', 'day'],
  ai_reports: [
    'id',
    'ai_id',
    'reporter_account_id',
    'reason',
    'details',
    'status',
    'created_at',
    'resolution_note',
  ],
  colony_skin_reports: [
    'id',
    'version_id',
    'reporter_account_id',
    'reason',
    'created_at',
    'resolution',
    'resolved_at',
    'resolved_by_account_id',
    'resolution_reason',
  ],
  colony_skin_drafts: [
    'account_id',
    'revision',
    'name',
    'building_color',
    'image',
    'material',
    'updated_at',
    'skin_id',
    'swarm_mesh',
    'swarm_view_angle',
  ],
  skin_purchases: [
    'id',
    'account_id',
    'request_id',
    'sku',
    'entitlement',
    'price_id',
    'checkout_id',
    'payment_intent_id',
    'status',
    'entitlement_id',
    'created_at',
    'updated_at',
    'reconcile_after',
    'recovery_cursor',
  ],
  skin_payment_events: ['id', 'event_type', 'purchase_id', 'processed_at'],
  colony_skins: [
    'id',
    'owner_account_id',
    'kind',
    'name',
    'entitlement',
    'disabled_at',
    'created_at',
  ],
  colony_skin_versions: [
    'id',
    'skin_id',
    'texture_sha256',
    'material_sha256',
    'layout',
    'building_color',
    'manifest_sha256',
    'created_at',
    'swarm_mesh',
    'swarm_view_angle',
  ],
  colony_skin_equipment: ['account_id', 'version_id', 'updated_at', 'building_color'],
  match_colony_skins: [
    'building_color',
    'match_id',
    'team_index',
    'account_id',
    'version_id',
    'assertion',
    'created_at',
  ],
  studio_threads: [
    'id',
    'account_id',
    'title',
    'brief',
    'created_at',
    'updated_at',
    'event_cursor',
  ],
  studio_events: ['thread_id', 'cursor', 'request_id', 'dedup', 'type', 'payload', 'created_at'],
  studio_provider_usage: ['day', 'calls'],
  studio_artifacts: [
    'id',
    'thread_id',
    'request_id',
    'stage',
    'kind',
    'label',
    'hash',
    'width',
    'height',
    'created_at',
  ],
  studio_messages: ['id', 'thread_id', 'role', 'text', 'created_at'],
  studio_requests: [
    'id',
    'thread_id',
    'account_id',
    'kind',
    'status',
    'input',
    'checkpoints',
    'lease_until',
    'lease',
    'map_id',
    'map_hash',
    'error',
    'charged',
    'created_at',
    'completed_at',
  ],
  studio_attempts: [
    'id',
    'request_id',
    'stage',
    'model',
    'status',
    'input',
    'output',
    'created_at',
  ],
  map_wallets: ['account_id', 'balance', 'reserved'],
  map_ledger: ['id', 'account_id', 'amount', 'kind', 'details', 'created_at'],
  map_calls: ['id', 'account_id', 'reserved', 'status', 'charged', 'rate', 'usage', 'created_at'],
  hive_wallets: ['account_id', 'balance', 'reserved'],
  hive_ledger: ['id', 'account_id', 'amount', 'kind', 'details', 'created_at'],
  hive_calls: ['id', 'account_id', 'reserved', 'status', 'charged', 'rate', 'usage', 'created_at'],
  hive_sessions: [
    'id',
    'account_id',
    'match_id',
    'seat',
    'team',
    'client_id',
    'lease',
    'lease_until',
    'tick',
    'generation',
    'supervision',
    'pending_run',
    'run_id',
    'run_until',
    'last_wake_tick',
    'wake_window',
    'wake_count',
    'created_at',
  ],
  hive_events: ['id', 'session_id', 'dedup', 'kind', 'body', 'created_at'],
  hive_operations: [
    'supervised',
    'id',
    'session_id',
    'generation',
    'lease',
    'status',
    'request',
    'result',
    'created_at',
  ],
  hive_programs: ['session_id', 'id', 'revision', 'definition', 'status', 'supervised'],
  map_purchases: [
    'id',
    'account_id',
    'checkout_id',
    'payment_id',
    'pack',
    'paid',
    'reversed',
    'created_at',
  ],
  hive_purchases: [
    'id',
    'account_id',
    'checkout_id',
    'payment_id',
    'pack',
    'paid',
    'reversed',
    'created_at',
  ],
  accounts: [
    'avatar_source',
    'avatar_key',
    'avatar_revision',
    'gravatar_fingerprint',
    'gravatar_checked_at',
    'gravatar_key',
    'id',
    'kind',
    'display_name',
    'role',
    'status',
    'muted_until',
    'created_at',
    'updated_at',
    'last_seen_at',
    'display_name_changed_at',
    'deleted_at',
  ],
  identities: [
    'id',
    'account_id',
    'provider',
    'subject',
    'email',
    'password_hash',
    'created_at',
    'last_used_at',
  ],
  device_credentials: [
    'id',
    'account_id',
    'credential_hash',
    'platform',
    'created_at',
    'last_used_at',
    'revoked_at',
  ],
  refresh_tokens: [
    'id',
    'account_id',
    'family_id',
    'token_hash',
    'client_platform',
    'issued_at',
    'expires_at',
    'rotated_at',
    'revoked_at',
    'replaced_by',
    'grace_uses',
  ],
  signin_attempts: [
    'id',
    'confirmation_code',
    'provider',
    'status',
    'requesting_account_id',
    'account_id',
    'created_at',
    'expires_at',
    'completed_at',
    'resume_hash',
    'mode',
    'client_platform',
    'browser_binding_hash',
    'conflict_account_id',
    'failure_reason',
    'linked',
    'delivered_at',
    'code_confirmed_at',
    'code_failures',
    'requesting_network_hash',
  ],
  web_sessions: [
    'id',
    'account_id',
    'token_hash',
    'created_at',
    'expires_at',
    'last_used_at',
    'revoked_at',
  ],
  auth_flows: [
    'id',
    'state_hash',
    'provider',
    'code_verifier',
    'nonce',
    'purpose',
    'attempt_id',
    'browser_binding_hash',
    'created_at',
    'expires_at',
    'consumed_at',
  ],
  entitlements: [
    'id',
    'account_id',
    'entitlement',
    'source',
    'granted_at',
    'expires_at',
    'revoked_at',
  ],
  admin_audit_log: [
    'id',
    'actor_account_id',
    'action',
    'target_type',
    'target_id',
    'details',
    'created_at',
  ],
  blobs: [
    'sha256',
    'size',
    'content_type',
    'storage_key',
    'visibility',
    'owner_account_id',
    'created_at',
  ],
  relays: [
    'id',
    'public_url',
    'region',
    'build',
    'turn_protocol',
    'max_matches',
    'active_matches',
    'connections',
    'cpu',
    'draining',
    'registered_at',
    'last_heartbeat_at',
  ],
  engine_agents: ['id', 'sim_version', 'kinds', 'build', 'started_at', 'last_seen_at'],
  engine_jobs: [
    'id',
    'kind',
    'sim_version',
    'payload',
    'status',
    'result',
    'error',
    'agent_id',
    'created_at',
    'completed_at',
    'attempts',
    'max_attempts',
    'leased_by',
    'lease_token_hash',
    'lease_expires_at',
    'reported_at',
    'match_id',
  ],
  maps: [
    'authoring',
    'id',
    'owner_account_id',
    'title',
    'description',
    'visibility',
    'hidden',
    'hidden_reason',
    'play_count',
    'download_count',
    'created_at',
    'updated_at',
    'made_with',
    'generator',
    'like_count',
    'latest_version_id',
    'hidden_at',
    'hidden_by_account_id',
  ],
  map_versions: [
    'id',
    'map_id',
    'hash',
    'size',
    'width',
    'height',
    'team_count',
    'min_version_minor',
    'preview_hash',
    'validation',
    'validation_error',
    'created_at',
    'sim_version',
    'validate_job_id',
    'preview_job_id',
    'preview_status',
    'preview_width',
    'preview_height',
    'file_title',
    'uploader_account_id',
    'notes',
  ],
  map_likes: ['map_id', 'account_id', 'created_at'],
  map_reports: [
    'id',
    'map_id',
    'reporter_account_id',
    'reason',
    'details',
    'status',
    'resolved_by_account_id',
    'created_at',
    'resolved_at',
    'resolution_note',
  ],
  rooms: [
    'id',
    'code',
    'name',
    'visibility',
    'status',
    'host_account_id',
    'sim_version',
    'settings',
    'revision',
    'match_id',
    'created_at',
    'updated_at',
    'closed_at',
    'starting_since',
    'notice',
  ],
  room_kicks: ['room_id', 'account_id', 'kicked_by_account_id', 'until', 'created_at'],
  map_downloads: ['map_id', 'downloader', 'day'],
  room_members: ['room_id', 'account_id', 'connected', 'joined_at', 'last_seen_at', 'region_rtts'],
  room_seats: [
    'room_id',
    'seat',
    'team',
    'occupant',
    'account_id',
    'ai_id',
    'ai_name',
    'ready',
    'locked',
  ],
  room_chat_messages: ['id', 'room_id', 'account_id', 'text', 'sent_at'],
  matches: [
    'skins_frozen_at',
    'id',
    'sim_version',
    'origin',
    'room_id',
    'queue_id',
    'rated',
    'status',
    'verification',
    'setup',
    'seed',
    'map_hash',
    'relay_id',
    'end_reason',
    'final_tick',
    'desync_flagged',
    'created_at',
    'started_at',
    'ended_at',
    'rating_status',
    'rating_note',
    'ratings_applied_at',
    'proposal_id',
    'relay_assigned_at',
    'relay_attempts',
    'end_report',
    'relay_seen_at',
  ],
  rating_entities: ['id', 'kind', 'account_id', 'ai_id', 'ai_sim_version', 'created_at'],
  ratings: [
    'entity_id',
    'ladder',
    'mu',
    'sigma',
    'ordinal',
    'games',
    'wins',
    'last_match_id',
    'updated_at',
    'seed_source',
  ],
  match_participants: [
    'match_id',
    'seat',
    'team',
    'kind',
    'account_id',
    'ai_id',
    'rating_entity_id',
    'display_name',
    'outcome',
    'disconnects',
    'quit_tick',
    'network',
    'rating_before',
    'rating_after',
  ],
  match_team_stats: [
    'match_id',
    'team',
    'outcome',
    'prestige',
    'eliminated_tick',
    'statistics',
    'timeline',
  ],
  match_artifacts: ['match_id', 'kind', 'blob_sha256', 'created_at'],
  queue_tickets: [
    'id',
    'queue_id',
    'account_id',
    'sim_version',
    'region_rtts',
    'rating_mu',
    'rating_sigma',
    'status',
    'match_id',
    'created_at',
    'updated_at',
    'allow_ai_opponent',
    'proposal_id',
    'search_id',
  ],
  rating_history: [
    'match_id',
    'entity_id',
    'ladder',
    'result',
    'mu_before',
    'sigma_before',
    'mu_after',
    'sigma_after',
    'display_before',
    'display_after',
    'created_at',
  ],
  match_proposals: [
    'id',
    'queue_id',
    'sim_version',
    'region',
    'rated',
    'backfilled',
    'status',
    'map',
    'expires_at',
    'match_id',
    'start_attempts',
    'failure',
    'created_at',
    'resolved_at',
  ],
  match_proposal_seats: [
    'proposal_id',
    'slot',
    'side',
    'kind',
    'ticket_id',
    'account_id',
    'ai_id',
    'rating_entity_id',
    'mu',
    'sigma',
    'response',
    'responded_at',
  ],
  queue_cooldowns: ['account_id', 'until', 'reason', 'created_at'],
  map_uploads: [
    'id',
    'owner_account_id',
    'blob_sha256',
    'format',
    'sim_version',
    'file_name',
    'status',
    'job_id',
    'width',
    'height',
    'team_count',
    'version_minor',
    'title',
    'players',
    'failure',
    'created_at',
    'completed_at',
  ],
  generated_maps: [
    'descriptor_hash',
    'sim_version',
    'descriptor',
    'status',
    'job_id',
    'map_hash',
    'width',
    'height',
    'team_count',
    'failure',
    'created_at',
    'completed_at',
  ],
  warm_maps: [
    'id',
    'queue_id',
    'sim_version',
    'entry_key',
    'descriptor_hash',
    'match_id',
    'created_at',
    'taken_at',
  ],
  rate_limits: ['bucket', 'key', 'window_start', 'count', 'previous_count'],
  account_name_scrubs: ['account_id', 'match_id', 'created_at'],
  leader_leases: ['name', 'epoch', 'holder', 'acquired_at', 'renewed_at'],
  notification_payloads: ['id', 'channel', 'payload', 'created_at'],
  api_replicas: ['id', 'started_at', 'heartbeat_at'],
  realtime_presence: ['account_id', 'replica_id', 'since'],
  match_results_view: [
    'match_id',
    'origin',
    'queue_id',
    'rated',
    'sim_version',
    'map_hash',
    'generator_id',
    'final_tick',
    'ended_at',
    'seat',
    'team',
    'kind',
    'account_id',
    'ai_id',
    'rating_entity_id',
    'outcome',
    'won',
  ],
  recent_win_rates_view: [
    'account_id',
    'ai_id',
    'ai_sim_version',
    'dimension',
    'key',
    'games',
    'wins',
    'win_rate',
    'last_played_at',
  ],
  recent_game_lengths_view: [
    'dimension',
    'key',
    'games',
    'mean_ticks',
    'median_ticks',
    'p90_ticks',
    'max_ticks',
  ],
  team_timeline_view: [
    'match_id',
    'team',
    'tick',
    'units',
    'buildings',
    'prestige',
    'hp',
    'attack',
    'defense',
  ],
  account_economy_curves_view: [
    'account_id',
    'match_id',
    'queue_id',
    'ended_at',
    'tick',
    'units',
    'buildings',
    'prestige',
    'average_units',
    'average_buildings',
    'average_prestige',
    'games_at_tick',
  ],
};

const SIM = `125-49-${'3f'.repeat(32)}`;
const HASH = 'ab'.repeat(32);
const HASH2 = 'cd'.repeat(32);

let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase({ migrate: false, role: 'migrator' });
});

afterAll(async () => {
  await database?.drop();
});

describe('migrations', () => {
  it('upgrades the online foundation without replacing account data', async () => {
    const existing = await createTestDatabase({ migrate: false, role: 'migrator' });
    try {
      const foundation = await createMigrator(existing.db).migrateTo('0017_retention');
      expect(foundation.error).toBeUndefined();
      const account = await existing.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: 'Existing commander' })
        .returning('id')
        .executeTakeFirstOrThrow();
      expect((await migrateToLatest(existing.db)).map((r) => r.migrationName)).toEqual([
        '0018_realtime_presence',
        '0019_warm_maps_over_generated',
        '0020_hive',
        '0021_hive_supervision',
        '0022_map_studio',
        '0023_colony_skins',
        '0024_match_skin_snapshot',
        '0025_skin_purchases',
        '0026_skin_building_color',
        '0027_skin_payment_reconciliation',
        '0028_skin_checkout_recovery',
        '0029_skin_drafts',
        '0030_skin_draft_source',
        '0031_skin_reports',
        '0032_signin_same_network',
        '0033_queue_searches',
        '0034_skin_swarm_mesh',
        '0035_colony_skins_v2',
        '0036_players_avatars',
        '0037_studio_events',
        '0038_skin_view_angle',
        '0039_ai_library',
      ]);
      expect(
        (
          await existing.db
            .selectFrom('accounts')
            .select('display_name')
            .where('id', '=', account.id)
            .executeTakeFirstOrThrow()
        ).display_name,
      ).toBe('Existing commander');
      await existing.db.insertInto('hive_wallets').values({ account_id: account.id }).execute();
      expect(
        (
          await existing.db
            .selectFrom('hive_wallets')
            .select('balance')
            .where('account_id', '=', account.id)
            .executeTakeFirstOrThrow()
        ).balance,
      ).toBe(0);
      expect(await migrateToLatest(existing.db)).toEqual([]);
    } finally {
      await existing.drop();
    }
  });
  it('apply from an empty database and are idempotent', async () => {
    const first = await migrateToLatest(database.db);
    const files = Object.keys(await new SqlFileMigrationProvider().getMigrations());
    expect(files.slice(0, 3)).toEqual([
      '0001_initial',
      '0002_ratings_matchmaking',
      '0003_identity',
    ]);
    expect(first.map((r) => [r.migrationName, r.status])).toEqual(
      files.map((name) => [name, 'Success']),
    );
    expect(await migrateToLatest(database.db)).toEqual([]);
    const status = await createMigrator(database.db).getMigrations();
    expect(status.every((m) => m.executedAt instanceof Date)).toBe(true);
  });

  it('upgrades an existing platform database through all skin migrations', async () => {
    const existing = await createTestDatabase({ migrate: false, role: 'migrator' });
    try {
      const old = await createMigrator(existing.db).migrateTo('0022_map_studio');
      expect(old.error).toBeUndefined();
      const account = await existing.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: 'Existing colony' })
        .returning('id')
        .executeTakeFirstOrThrow();
      const thread = (
        await sql<{
          id: string;
        }>`INSERT INTO studio_threads(account_id,title) VALUES(${account.id},'Legacy project') RETURNING id`.execute(
          existing.db,
        )
      ).rows[0]!;
      const request = (
        await sql<{
          id: string;
        }>`INSERT INTO studio_requests(id,thread_id,account_id,kind,status,input) VALUES(gen_random_uuid(),${thread.id},${account.id},'chat','ready','{}') RETURNING id`.execute(
          existing.db,
        )
      ).rows[0]!;
      await sql`INSERT INTO studio_attempts(id,request_id,stage,model,status,input,created_at) VALUES(gen_random_uuid(),${request.id},'failed','fixture','failed','{}','2020-01-01T23:30:00-05:00'),(gen_random_uuid(),${request.id},'uncertain','fixture','uncertain','{}','2020-01-02T23:59:00Z')`.execute(
        existing.db,
      );
      const upgraded = await migrateToLatest(existing.db);
      expect(upgraded).toHaveLength(17);
      expect(upgraded.every((migration) => migration.status === 'Success')).toBe(true);
      expect(
        await existing.db
          .selectFrom('accounts')
          .select('display_name')
          .where('id', '=', account.id)
          .executeTakeFirstOrThrow(),
      ).toEqual({ display_name: 'Existing colony' });
      expect(
        (
          await sql<{
            day: string;
            calls: string;
          }>`SELECT day::text,calls::text FROM studio_provider_usage`.execute(existing.db)
        ).rows,
      ).toEqual([{ day: '2020-01-02', calls: '2' }]);
      expect(await migrateToLatest(existing.db)).toEqual([]);
    } finally {
      await existing.drop();
    }
  });

  it('keeps published skin versions on the classic swarm and keys versions by mesh', async () => {
    const existing = await createTestDatabase({ migrate: false, role: 'migrator' });
    try {
      expect(
        (await createMigrator(existing.db).migrateTo('0033_queue_searches')).error,
      ).toBeUndefined();
      const owner = await existing.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: 'Painter' })
        .returning('id')
        .executeTakeFirstOrThrow();
      const skin = await existing.db
        .insertInto('colony_skins')
        .values({
          owner_account_id: owner.id,
          kind: 'custom',
          name: 'Old',
          entitlement: 'skins:designer',
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      await existing.db
        .insertInto('blobs')
        .values({
          sha256: 'a'.repeat(64),
          size: 1,
          storage_key: 'skins/a',
          content_type: 'image/png',
          visibility: 'private',
          owner_account_id: owner.id,
        })
        .execute();
      // Colony-v1 rows predate the typed colony-v2 schema, so insert them directly.
      const publish = (manifest: string, swarmMesh?: string) =>
        sql<{ id: string }>`
          INSERT INTO colony_skin_versions
            (skin_id, texture_sha256, layout, building_color, manifest_sha256${
              swarmMesh === undefined ? sql`` : sql`, swarm_mesh`
            })
          VALUES (${skin.id}, ${'a'.repeat(64)}, 'colony-v1', 7, ${manifest.repeat(64)}${
            swarmMesh === undefined ? sql`` : sql`, ${swarmMesh}`
          })
          RETURNING id`.execute(existing.db);
      const published = (await publish('b')).rows[0]!;
      expect(
        (await createMigrator(existing.db).migrateTo('0034_skin_swarm_mesh')).error,
      ).toBeUndefined();
      expect(
        await existing.db
          .selectFrom('colony_skin_versions')
          .select('swarm_mesh')
          .where('id', '=', published.id)
          .executeTakeFirstOrThrow(),
      ).toEqual({ swarm_mesh: 'classic' });
      // The same paint may be published again for another mesh, but only once per mesh.
      await publish('c', 'crown');
      await expect(publish('d', 'crown')).rejects.toThrow(/colony_skin_versions_content_key/);
      await expect(publish('e', 'Crown!')).rejects.toThrow(/check constraint/);
    } finally {
      await existing.drop();
    }
  });

  it('replaces colony-v1 skins with colony-v2, keeping purchases and preset products', async () => {
    const existing = await createTestDatabase({ migrate: false, role: 'migrator' });
    try {
      const db = existing.db;
      expect((await createMigrator(db).migrateTo('0034_skin_swarm_mesh')).error).toBeUndefined();
      const sha = (c: string) => c.repeat(64);
      const v1 = await sql<{ account: string; preset: string; custom: string }>`
        WITH account AS (
          INSERT INTO accounts (kind, display_name) VALUES ('registered', 'V1 painter') RETURNING id
        ), blob AS (
          INSERT INTO blobs (sha256, size, content_type, storage_key)
          VALUES (${sha('a')}, 1, 'image/png', 'sha256/a') RETURNING sha256
        ), preset AS (
          INSERT INTO colony_skins (kind, name, entitlement)
          VALUES ('preset', 'Preset', 'skins:stripes') RETURNING id
        ), custom AS (
          INSERT INTO colony_skins (kind, owner_account_id, name, entitlement)
          SELECT 'custom', id, 'Custom', 'skins:designer' FROM account RETURNING id
        ), grant_ AS (
          INSERT INTO entitlements (account_id, entitlement, source)
          SELECT id, 'skins:designer', 'test' FROM account
        )
        SELECT (SELECT id FROM account) AS account, (SELECT id FROM preset) AS preset,
          (SELECT id FROM custom) AS custom, (SELECT sha256 FROM blob) AS blob`.execute(db);
      const { account, preset, custom } = v1.rows[0]!;
      const version = await sql<{ id: string }>`
        INSERT INTO colony_skin_versions
          (skin_id, texture_sha256, layout, building_color, swarm_mesh, manifest_sha256)
        VALUES (${custom}, ${sha('a')}, 'colony-v1', 1, 'crown', ${sha('b')}) RETURNING id`.execute(
        db,
      );
      const versionId = version.rows[0]!.id;
      await sql`INSERT INTO colony_skin_equipment (account_id, version_id) VALUES (${account}, ${versionId})`.execute(
        db,
      );
      await sql`INSERT INTO colony_skin_reports (version_id, reporter_account_id, reason)
        VALUES (${versionId}, ${account}, 'Report')`.execute(db);
      await sql`INSERT INTO colony_skin_drafts (account_id, name, building_color, image, skin_id)
        VALUES (${account}, 'Draft', 1, '\x01', ${custom})`.execute(db);

      const upgraded = await migrateToLatest(db);
      expect(upgraded.map((m) => [m.migrationName, m.status])).toEqual([
        ['0035_colony_skins_v2', 'Success'],
        ['0036_players_avatars', 'Success'],
        ['0037_studio_events', 'Success'],
        ['0038_skin_view_angle', 'Success'],
        ['0039_ai_library', 'Success'],
      ]);
      for (const table of [
        'colony_skin_versions',
        'colony_skin_equipment',
        'colony_skin_reports',
        'colony_skin_drafts',
        'match_colony_skins',
      ] as const)
        expect(await db.selectFrom(table).selectAll().execute(), table).toEqual([]);
      expect((await db.selectFrom('colony_skins').select('id').execute()).map((r) => r.id)).toEqual(
        [preset],
      );
      expect(
        await db
          .selectFrom('entitlements')
          .select('entitlement')
          .where('account_id', '=', account)
          .execute(),
      ).toEqual([{ entitlement: 'skins:designer' }]);

      await db
        .insertInto('blobs')
        .values({ sha256: sha('c'), size: 1, content_type: 'image/png', storage_key: 'sha256/c' })
        .execute();
      const v2 = {
        skin_id: preset,
        texture_sha256: sha('a'),
        material_sha256: sha('c'),
        layout: 'colony-v2' as const,
        building_color: 1,
      };
      const inserted = await db
        .insertInto('colony_skin_versions')
        .values({ ...v2, manifest_sha256: sha('d') })
        .returning('id')
        .executeTakeFirstOrThrow();
      await expect(
        db
          .insertInto('colony_skin_versions')
          .values({ ...v2, manifest_sha256: sha('e') })
          .execute(),
      ).rejects.toThrow(/colony_skin_versions_content_key/);
      // The same paint on another swarm mesh is a distinct version.
      await db
        .insertInto('colony_skin_versions')
        .values({ ...v2, swarm_mesh: 'crown', manifest_sha256: sha('9') })
        .execute();
      await expect(
        db
          .insertInto('colony_skin_versions')
          .values({ ...v2, layout: 'colony-v1' as 'colony-v2', manifest_sha256: sha('f') })
          .execute(),
      ).rejects.toThrow(/layout_check/);
      await expect(
        db.deleteFrom('colony_skin_versions').where('id', '=', inserted.id).execute(),
      ).rejects.toThrow(/immutable/);
      const draft = {
        account_id: account,
        revision: crypto.randomUUID(),
        name: 'Draft',
        building_color: 1,
        image: Buffer.alloc(1048576, 1),
      };
      await expect(
        db
          .insertInto('colony_skin_drafts')
          .values({ ...draft, material: Buffer.alloc(262145, 1) })
          .execute(),
      ).rejects.toThrow(/material_check/);
      await db
        .insertInto('colony_skin_drafts')
        .values({ ...draft, material: Buffer.alloc(262144, 1) })
        .execute();
    } finally {
      await existing.drop();
    }
  });

  it('preserves legacy skin content at angle zero and keys new versions by validated angle', async () => {
    const existing = await createTestDatabase({ migrate: false, role: 'migrator' });
    try {
      const db = existing.db;
      expect((await createMigrator(db).migrateTo('0037_studio_events')).error).toBeUndefined();
      const owner = await db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: 'Camera painter' })
        .returning('id')
        .executeTakeFirstOrThrow();
      const skin = await db
        .insertInto('colony_skins')
        .values({
          owner_account_id: owner.id,
          kind: 'custom',
          name: 'Existing view',
          entitlement: 'skins:designer',
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      await db
        .insertInto('blobs')
        .values({ sha256: HASH, size: 1, content_type: 'image/png', storage_key: 'skins/camera' })
        .execute();
      const content = {
        skin_id: skin.id,
        texture_sha256: HASH,
        material_sha256: HASH,
        layout: 'colony-v2' as const,
        building_color: 7,
        swarm_mesh: 'crown',
      };
      const version = await db
        .insertInto('colony_skin_versions')
        .values({ ...content, manifest_sha256: HASH2 })
        .returning('id')
        .executeTakeFirstOrThrow();
      const draft = {
        account_id: owner.id,
        revision: crypto.randomUUID(),
        skin_id: skin.id,
        name: 'Existing draft',
        building_color: 7,
        swarm_mesh: 'crown',
        image: Buffer.from([1, 2, 3]),
        material: Buffer.from([0, 1]),
      };
      await db.insertInto('colony_skin_drafts').values(draft).execute();
      expect((await migrateToLatest(db)).map((m) => [m.migrationName, m.status])).toEqual([
        ['0038_skin_view_angle', 'Success'],
        ['0039_ai_library', 'Success'],
      ]);
      expect(
        await db
          .selectFrom('colony_skin_versions')
          .select([
            'texture_sha256',
            'material_sha256',
            'manifest_sha256',
            'swarm_mesh',
            'swarm_view_angle',
          ])
          .where('id', '=', version.id)
          .executeTakeFirstOrThrow(),
      ).toEqual({
        texture_sha256: HASH,
        material_sha256: HASH,
        manifest_sha256: HASH2,
        swarm_mesh: 'crown',
        swarm_view_angle: 0,
      });
      expect(
        await db
          .selectFrom('colony_skin_drafts')
          .selectAll()
          .where('account_id', '=', owner.id)
          .executeTakeFirstOrThrow(),
      ).toMatchObject({
        ...draft,
        swarm_view_angle: 0,
      });
      const publishAt = (angle: number, manifest: string) =>
        db
          .insertInto('colony_skin_versions')
          .values({ ...content, swarm_view_angle: angle, manifest_sha256: manifest.repeat(64) })
          .execute();
      // Identical paint may have another final view, while an identical angle
      // still deduplicates and invalid angles cannot be stored directly in SQL.
      await publishAt(359, 'e');
      await expect(publishAt(359, 'f')).rejects.toThrow(/colony_skin_versions_content_key/);
      await expect(publishAt(0, '1')).rejects.toThrow(/colony_skin_versions_content_key/);
      for (const angle of [-1, 360]) {
        await expect(publishAt(angle, '2')).rejects.toThrow(/swarm_view_angle_check/);
        await expect(
          db
            .updateTable('colony_skin_drafts')
            .set({ swarm_view_angle: angle })
            .where('account_id', '=', owner.id)
            .execute(),
        ).rejects.toThrow(/swarm_view_angle_check/);
      }
      await db
        .updateTable('colony_skin_drafts')
        .set({ swarm_view_angle: 359 })
        .where('account_id', '=', owner.id)
        .execute();
      expect(
        (
          await db
            .selectFrom('colony_skin_drafts')
            .select('swarm_view_angle')
            .where('account_id', '=', owner.id)
            .executeTakeFirstOrThrow()
        ).swarm_view_angle,
      ).toBe(359);
      expect(await migrateToLatest(db)).toEqual([]);
    } finally {
      await existing.drop();
    }
  });

  it('match the typed schema column for column', async () => {
    const rows = await sql<{ table_name: string; column_name: string }>`
      SELECT table_name, column_name FROM information_schema.columns
      WHERE table_schema = 'public' AND table_name NOT LIKE 'platform_migrations%'
      ORDER BY table_name, ordinal_position`.execute(database.db);
    const actual: Record<string, string[]> = {};
    for (const row of rows.rows) (actual[row.table_name] ??= []).push(row.column_name);
    const expected = Object.fromEntries(
      Object.entries(typedColumns).map(([table, columns]) => [table, [...columns].sort()]),
    );
    expect(Object.fromEntries(Object.entries(actual).map(([t, c]) => [t, [...c].sort()]))).toEqual(
      expected,
    );
  });
});

describe('data model', () => {
  it('stores and links a full match lifecycle through the typed interface', async () => {
    const db = database.db;
    const alice = await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Alice' })
      .returningAll()
      .executeTakeFirstOrThrow();
    expect(alice.role).toBe('user');
    expect(alice.created_at).toBeInstanceOf(Date);
    const guest = await db
      .insertInto('accounts')
      .values({ kind: 'guest', display_name: 'Guest 4821' })
      .returning('id')
      .executeTakeFirstOrThrow();

    await db
      .insertInto('identities')
      .values({
        account_id: alice.id,
        provider: 'google',
        subject: '10769150350006150715113082367',
      })
      .execute();
    await db
      .insertInto('device_credentials')
      .values({ account_id: guest.id, credential_hash: HASH, platform: 'android' })
      .execute();
    await db
      .insertInto('refresh_tokens')
      .values({
        account_id: alice.id,
        family_id: crypto.randomUUID(),
        token_hash: HASH2,
        expires_at: new Date(Date.now() + 86_400_000),
      })
      .execute();
    await db
      .insertInto('signin_attempts')
      .values({ confirmation_code: 'KQ7M2X', expires_at: new Date(Date.now() + 600_000) })
      .execute();
    await db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: alice.id,
        action: 'grant-admin',
        target_type: 'account',
        target_id: alice.id,
      })
      .execute();

    await db
      .insertInto('blobs')
      .values([
        {
          sha256: HASH,
          size: 1234,
          content_type: 'application/x-glob2-map',
          storage_key: `blobs/ab/${HASH}`,
        },
        {
          sha256: HASH2,
          size: 99,
          content_type: 'application/octet-stream',
          storage_key: `blobs/cd/${HASH2}`,
        },
      ])
      .execute();
    await db
      .insertInto('relays')
      .values({
        id: 'relay-1',
        public_url: 'wss://relay.example.org/relay',
        region: 'eu-west',
        build: 'test',
        turn_protocol: 1,
        max_matches: 10,
      })
      .execute();
    await db
      .insertInto('engine_agents')
      .values({
        id: 'agent-1',
        sim_version: SIM,
        kinds: ['verify-match', 'generate-map'],
        build: 'test',
      })
      .execute();
    const agent = await db.selectFrom('engine_agents').selectAll().executeTakeFirstOrThrow();
    expect(agent.kinds).toEqual(['verify-match', 'generate-map']);
    await db
      .insertInto('engine_jobs')
      .values({
        kind: 'generate-map',
        sim_version: SIM,
        payload: JSON.stringify({ generator: {} }),
      })
      .execute();

    const map = await db
      .insertInto('maps')
      .values({ owner_account_id: alice.id, title: 'Four Corners', visibility: 'public' })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('map_versions')
      .values({ map_id: map.id, hash: HASH, size: 1234, width: 128, height: 128, team_count: 4 })
      .execute();
    await db.insertInto('map_likes').values({ map_id: map.id, account_id: guest.id }).execute();
    await db
      .insertInto('map_reports')
      .values({ map_id: map.id, reporter_account_id: guest.id, reason: 'broken' })
      .execute();

    const room = await db
      .insertInto('rooms')
      .values({
        code: 'K7QX2M',
        name: "Alice's room",
        visibility: 'link',
        host_account_id: alice.id,
        sim_version: SIM,
        settings: JSON.stringify({ rules: STANDARD_RULES, teams: [], experiments: [] }),
      })
      .returningAll()
      .executeTakeFirstOrThrow();
    expect((room.settings as { rules: unknown }).rules).toEqual(STANDARD_RULES);
    await db
      .insertInto('room_members')
      .values({ room_id: room.id, account_id: alice.id })
      .execute();
    await db
      .insertInto('room_seats')
      .values([
        {
          room_id: room.id,
          seat: 0,
          team: 0,
          occupant: 'human',
          account_id: alice.id,
          ready: true,
        },
        { room_id: room.id, seat: 1, team: 1, occupant: 'ai', ai_id: 'cortex', ai_name: 'AI 2' },
      ])
      .execute();
    await db
      .insertInto('room_chat_messages')
      .values({ room_id: room.id, account_id: alice.id, text: 'gl hf' })
      .execute();

    const match = await db
      .insertInto('matches')
      .values({
        sim_version: SIM,
        origin: 'queue',
        queue_id: 'ranked-1v1',
        rated: true,
        setup: JSON.stringify({ schemaVersion: 1 }),
        seed: 4294967295,
        map_hash: HASH,
        relay_id: 'relay-1',
      })
      .returningAll()
      .executeTakeFirstOrThrow();
    expect(match.seed).toBe(4294967295);
    await db
      .updateTable('rooms')
      .set({ match_id: match.id, status: 'in_match' })
      .where('id', '=', room.id)
      .execute();

    const [person, ai] = await db
      .insertInto('rating_entities')
      .values([
        { kind: 'account', account_id: alice.id },
        { kind: 'ai', ai_id: 'cortex', ai_sim_version: SIM },
      ])
      .returning('id')
      .execute();
    await db
      .insertInto('ratings')
      .values([
        { entity_id: person!.id, ladder: 'ranked-1v1', mu: 25, sigma: 25 / 3 },
        { entity_id: ai!.id, ladder: 'ranked-1v1', mu: 30, sigma: 4 },
      ])
      .execute();
    const top = await db
      .selectFrom('ratings')
      .select(['entity_id', 'ordinal'])
      .where('ladder', '=', 'ranked-1v1')
      .orderBy('ordinal', 'desc')
      .execute();
    expect(top[0]!.entity_id).toBe(ai!.id);
    expect(top[0]!.ordinal).toBeCloseTo(18);

    await db
      .insertInto('match_participants')
      .values([
        {
          match_id: match.id,
          seat: 0,
          team: 0,
          kind: 'human',
          account_id: alice.id,
          rating_entity_id: person!.id,
          display_name: 'Alice',
          outcome: 'won',
        },
        {
          match_id: match.id,
          seat: 1,
          team: 1,
          kind: 'ai',
          ai_id: 'cortex',
          rating_entity_id: ai!.id,
          display_name: 'AI 2',
          outcome: 'lost',
        },
      ])
      .execute();
    await db
      .insertInto('match_team_stats')
      .values({
        match_id: match.id,
        team: 0,
        outcome: 'won',
        prestige: 210,
        statistics: JSON.stringify({ units: 61 }),
      })
      .execute();
    await db
      .insertInto('match_artifacts')
      .values({ match_id: match.id, kind: 'record', blob_sha256: HASH2 })
      .execute();
    await db
      .insertInto('queue_tickets')
      .values({
        queue_id: 'ranked-1v1',
        account_id: guest.id,
        sim_version: SIM,
        region_rtts: JSON.stringify([{ region: 'eu-west', rttMs: 30 }]),
      })
      .execute();
    await db
      .insertInto('entitlements')
      .values({ account_id: alice.id, entitlement: 'supporter', source: 'test' })
      .execute();

    const history = await db
      .selectFrom('match_participants as p')
      .innerJoin('matches as m', 'm.id', 'p.match_id')
      .select(['m.id', 'p.outcome', 'm.queue_id'])
      .where('p.account_id', '=', alice.id)
      .execute();
    expect(history).toEqual([{ id: match.id, outcome: 'won', queue_id: 'ranked-1v1' }]);
  });

  it('enforces the integrity rules', async () => {
    const db = database.db;
    const account = await db
      .insertInto('accounts')
      .values({ kind: 'guest', display_name: 'Rules' })
      .returning('id')
      .executeTakeFirstOrThrow();
    // Bad hash domain.
    await expect(
      db
        .insertInto('blobs')
        .values({ sha256: 'XYZ', size: 1, content_type: 'x', storage_key: 'k' })
        .execute(),
    ).rejects.toThrow();
    // An AI rating entity needs a sim version.
    await expect(
      db.insertInto('rating_entities').values({ kind: 'ai', ai_id: 'numbi' }).execute(),
    ).rejects.toThrow();
    // One active ticket per account in each queue (a search may enter several queues).
    await db
      .insertInto('queue_tickets')
      .values({ queue_id: 'q', account_id: account.id, sim_version: SIM })
      .execute();
    await db
      .insertInto('queue_tickets')
      .values({ queue_id: 'q2', account_id: account.id, sim_version: SIM })
      .execute();
    await expect(
      db
        .insertInto('queue_tickets')
        .values({ queue_id: 'q', account_id: account.id, sim_version: SIM })
        .execute(),
    ).rejects.toThrow(/queue_tickets_one_active_idx/);
    // A queue match must name its queue.
    await expect(
      db
        .insertInto('matches')
        .values({ sim_version: SIM, origin: 'queue', setup: '{}', seed: 1, map_hash: HASH })
        .execute(),
    ).rejects.toThrow();
  });
});
