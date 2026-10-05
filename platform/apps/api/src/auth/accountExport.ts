// "Download my data": everything the platform stores about one account, as a
// JSON document its owner can keep (GET /api/v1/accounts/me/export; privacy
// policy, docs/mobile/privacy-policy.md). Secrets and other people's data are
// left out: password, credential and token hashes, sign-in confirmation codes,
// and who moderated the account.
//
// Keep this in step with the migrations: a new table or column that holds
// data about an account belongs here too (test/accountExport.test.ts lists
// the tables with an account column and fails on one this file does not read).
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import { ACCOUNT_EXPORT_FORMAT, type AccountExport } from '@glob2/protocol';

/** Tables with account data this export reads, by the column naming the account. */
export const EXPORTED_ACCOUNT_COLUMNS: Record<string, string[]> = {
  accounts: ['id'],
  identities: ['account_id'],
  device_credentials: ['account_id'],
  refresh_tokens: ['account_id'],
  web_sessions: ['account_id'],
  signin_attempts: ['account_id', 'requesting_account_id'],
  entitlements: ['account_id'],
  admin_audit_log: ['target_id'],
  rating_entities: ['account_id'],
  match_participants: ['account_id'],
  rooms: ['host_account_id'],
  room_members: ['account_id'],
  room_seats: ['account_id'],
  room_chat_messages: ['account_id'],
  room_kicks: ['account_id'],
  queue_tickets: ['account_id'],
  queue_cooldowns: ['account_id'],
  match_proposal_seats: ['account_id'],
  colony_skins: ['owner_account_id'],
  colony_skin_equipment: ['account_id'],
  colony_skin_drafts: ['account_id'],
  match_colony_skins: ['account_id'],
  skin_purchases: ['account_id'],
  colony_skin_reports: ['reporter_account_id'],
  maps: ['owner_account_id'],
  map_likes: ['account_id'],
  map_reports: ['reporter_account_id'],
  map_uploads: ['owner_account_id'],
  map_downloads: ['downloader'],
  hive_wallets: ['account_id'],
  hive_ledger: ['account_id'],
  hive_calls: ['account_id'],
  hive_sessions: ['account_id'],
  hive_purchases: ['account_id'],
  map_wallets: ['account_id'],
  map_ledger: ['account_id'],
  map_calls: ['account_id'],
  map_purchases: ['account_id'],
  studio_threads: ['account_id'],
  studio_requests: ['account_id'],
};

/**
 * Account columns deliberately not exported, with the reason. Other people's
 * ids (who kicked, resolved, hid) or bookkeeping that holds nothing about the
 * account beyond what the export already lists.
 */
export const UNEXPORTED_ACCOUNT_COLUMNS: Record<string, string> = {
  'admin_audit_log.actor_account_id': 'the moderator who acted, not the account',
  'blobs.owner_account_id': 'the files are listed through maps, uploads and skins',
  'colony_skin_reports.resolved_by_account_id': 'the moderator who resolved a report',
  'map_reports.resolved_by_account_id': 'the moderator who resolved a report',
  'maps.hidden_by_account_id': 'the moderator who hid a map',
  'map_versions.uploader_account_id': 'versions are listed under the owner’s maps',
  'room_kicks.kicked_by_account_id': 'the host who kicked someone',
  'signin_attempts.conflict_account_id': 'another account the sign-in collided with',
  'account_name_scrubs.account_id': 'only exists for deleted accounts',
  'realtime_presence.account_id': 'which API server holds a live socket; cleared on disconnect',
};

type Row = Record<string, unknown>;

function camelKey(key: string): string {
  return key.replace(/_([a-z0-9])/g, (_m, c: string) => c.toUpperCase());
}

/** snake_case columns to camelCase keys, dates to RFC 3339, nulls dropped. */
function clean(row: Row): Row {
  const out: Row = {};
  for (const [key, value] of Object.entries(row)) {
    if (value === null || value === undefined) continue;
    out[camelKey(key)] = value instanceof Date ? value.toISOString() : value;
  }
  return out;
}

const rows = (list: Row[]): Row[] => list.map(clean);

/** Builds the export inside one read-only snapshot, so its sections agree. */
export async function exportAccount(
  db: Kysely<Database>,
  account: Account,
  origin: string,
): Promise<AccountExport> {
  const id = account.id;
  return db
    .transaction()
    .setIsolationLevel('repeatable read')
    .setAccessMode('read only')
    .execute(async (tx) => {
      const identities = await tx
        .selectFrom('identities')
        .select(['provider', 'subject', 'email', 'created_at', 'last_used_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const devices = await tx
        .selectFrom('device_credentials')
        .select(['id', 'platform', 'created_at', 'last_used_at', 'revoked_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const refreshTokens = await tx
        .selectFrom('refresh_tokens')
        .select(['client_platform', 'issued_at', 'expires_at', 'rotated_at', 'revoked_at'])
        .where('account_id', '=', id)
        .orderBy('issued_at')
        .execute();
      const webSessions = await tx
        .selectFrom('web_sessions')
        .select(['created_at', 'expires_at', 'last_used_at', 'revoked_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const signInAttempts = await tx
        .selectFrom('signin_attempts')
        .select([
          'id',
          'provider',
          'mode',
          'status',
          'client_platform',
          'failure_reason',
          'linked',
          'created_at',
          'completed_at',
        ])
        .where((eb) => eb.or([eb('account_id', '=', id), eb('requesting_account_id', '=', id)]))
        .orderBy('created_at')
        .execute();
      const entitlements = await tx
        .selectFrom('entitlements')
        .select(['entitlement', 'source', 'granted_at', 'expires_at', 'revoked_at'])
        .where('account_id', '=', id)
        .orderBy('granted_at')
        .execute();
      const moderation = await tx
        .selectFrom('admin_audit_log')
        .select(['action', 'details', 'created_at'])
        .where('target_type', '=', 'account')
        .where('target_id', '=', id)
        .orderBy('id')
        .execute();

      const ratingEntity = await tx
        .selectFrom('rating_entities')
        .select('id')
        .where('account_id', '=', id)
        .executeTakeFirst();
      const ratings = ratingEntity
        ? await tx
            .selectFrom('ratings')
            .select([
              'ladder',
              'mu',
              'sigma',
              'ordinal',
              'games',
              'wins',
              'seed_source',
              'updated_at',
            ])
            .where('entity_id', '=', ratingEntity.id)
            .orderBy('ladder')
            .execute()
        : [];
      const ratingHistory = ratingEntity
        ? await tx
            .selectFrom('rating_history')
            .select([
              'match_id',
              'ladder',
              'result',
              'mu_before',
              'sigma_before',
              'mu_after',
              'sigma_after',
              'display_before',
              'display_after',
              'created_at',
            ])
            .where('entity_id', '=', ratingEntity.id)
            .orderBy('created_at')
            .execute()
        : [];

      const matches = await tx
        .selectFrom('match_participants as p')
        .innerJoin('matches as m', 'm.id', 'p.match_id')
        .select([
          'm.id as match_id',
          'm.origin',
          'm.queue_id',
          'm.room_id',
          'm.rated',
          'm.status',
          'm.end_reason',
          'm.verification',
          'm.sim_version',
          'm.map_hash',
          'm.final_tick',
          'm.created_at',
          'm.started_at',
          'm.ended_at',
          'p.seat',
          'p.team',
          'p.display_name',
          'p.outcome',
          'p.disconnects',
          'p.quit_tick',
          'p.rating_before',
          'p.rating_after',
          'p.network',
        ])
        .where('p.account_id', '=', id)
        .orderBy('m.created_at')
        .execute();

      const roomsHosted = await tx
        .selectFrom('rooms')
        .select([
          'id',
          'code',
          'name',
          'visibility',
          'status',
          'settings',
          'notice',
          'match_id',
          'created_at',
          'closed_at',
        ])
        .where('host_account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const roomMemberships = await tx
        .selectFrom('room_members as rm')
        .innerJoin('rooms as r', 'r.id', 'rm.room_id')
        .select([
          'rm.room_id',
          'r.name as room_name',
          'rm.connected',
          'rm.joined_at',
          'rm.last_seen_at',
          'rm.region_rtts',
        ])
        .where('rm.account_id', '=', id)
        .orderBy('rm.joined_at')
        .execute();
      const roomSeats = await tx
        .selectFrom('room_seats')
        .select(['room_id', 'seat', 'team', 'ready'])
        .where('account_id', '=', id)
        .orderBy('room_id')
        .orderBy('seat')
        .execute();
      const roomChat = await tx
        .selectFrom('room_chat_messages')
        .select(['room_id', 'text', 'sent_at'])
        .where('account_id', '=', id)
        .orderBy('sent_at')
        .execute();
      const roomKicks = await tx
        .selectFrom('room_kicks')
        .select(['room_id', 'until', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();

      const queueTickets = await tx
        .selectFrom('queue_tickets')
        .select([
          'queue_id',
          'status',
          'region_rtts',
          'allow_ai_opponent',
          'rating_mu',
          'rating_sigma',
          'match_id',
          'created_at',
          'updated_at',
        ])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const queueCooldowns = await tx
        .selectFrom('queue_cooldowns')
        .select(['reason', 'until', 'created_at'])
        .where('account_id', '=', id)
        .execute();
      const matchProposals = await tx
        .selectFrom('match_proposal_seats as s')
        .innerJoin('match_proposals as mp', 'mp.id', 's.proposal_id')
        .select([
          's.proposal_id',
          'mp.queue_id',
          'mp.rated',
          'mp.status',
          's.response',
          's.responded_at',
          'mp.created_at',
        ])
        .where('s.account_id', '=', id)
        .orderBy('mp.created_at')
        .execute();

      const maps = await tx
        .selectFrom('maps')
        .select([
          'id',
          'title',
          'description',
          'visibility',
          'made_with',
          'authoring',
          'generator',
          'hidden',
          'hidden_reason',
          'play_count',
          'download_count',
          'like_count',
          'created_at',
          'updated_at',
        ])
        .where('owner_account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const mapVersions = maps.length
        ? await tx
            .selectFrom('map_versions')
            .select([
              'id',
              'map_id',
              'hash',
              'size',
              'width',
              'height',
              'team_count',
              'file_title',
              'notes',
              'validation',
              'created_at',
            ])
            .where(
              'map_id',
              'in',
              maps.map((m) => m.id),
            )
            .orderBy('created_at')
            .execute()
        : [];
      const mapLikes = await tx
        .selectFrom('map_likes')
        .select(['map_id', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const mapReports = await tx
        .selectFrom('map_reports')
        .select(['id', 'map_id', 'reason', 'details', 'status', 'created_at', 'resolved_at'])
        .where('reporter_account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const uploads = await tx
        .selectFrom('map_uploads')
        .select([
          'id',
          'format',
          'file_name',
          'title',
          'status',
          'width',
          'height',
          'team_count',
          'blob_sha256',
          'created_at',
        ])
        .where('owner_account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const mapDownloads = await tx
        .selectFrom('map_downloads')
        .select(['map_id', sql<string>`day::text`.as('day')])
        .where('downloader', '=', `a:${id}`)
        .orderBy('day')
        .execute();

      const skins = await tx
        .selectFrom('colony_skins')
        .select(['id', 'kind', 'name', 'entitlement', 'disabled_at', 'created_at'])
        .where('owner_account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const skinVersions = await tx
        .selectFrom('colony_skin_versions as v')
        .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
        .selectAll('v')
        .where('s.owner_account_id', '=', id)
        .orderBy('v.created_at')
        .execute();
      const skinEquipment = await tx
        .selectFrom('colony_skin_equipment')
        .select(['version_id', 'building_color', 'updated_at'])
        .where('account_id', '=', id)
        .execute();
      const skinDrafts = await tx
        .selectFrom('colony_skin_drafts')
        .select([
          'revision',
          'skin_id',
          'name',
          'building_color',
          'swarm_mesh',
          'image',
          'material',
          'updated_at',
        ])
        .where('account_id', '=', id)
        .execute();
      const skinMatches = await tx
        .selectFrom('match_colony_skins')
        .select(['match_id', 'team_index', 'version_id', 'building_color', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const skinPurchases = await tx
        .selectFrom('skin_purchases')
        .select([
          'id',
          'sku',
          'entitlement',
          'price_id',
          'status',
          'entitlement_id',
          'checkout_id',
          'payment_intent_id',
          'created_at',
          'updated_at',
        ])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const skinPaymentEvents = await tx
        .selectFrom('skin_payment_events as e')
        .innerJoin('skin_purchases as p', 'p.id', 'e.purchase_id')
        .select(['e.id', 'e.event_type', 'e.purchase_id', 'e.processed_at'])
        .where('p.account_id', '=', id)
        .orderBy('e.processed_at')
        .execute();
      const skinReports = await tx
        .selectFrom('colony_skin_reports')
        .select([
          'id',
          'version_id',
          'reason',
          'created_at',
          'resolution',
          'resolved_at',
          'resolution_reason',
        ])
        .where('reporter_account_id', '=', id)
        .orderBy('created_at')
        .execute();

      const hiveWallets = await tx
        .selectFrom('hive_wallets')
        .select(['balance', 'reserved'])
        .where('account_id', '=', id)
        .execute();
      const hiveLedger = await tx
        .selectFrom('hive_ledger')
        .select(['id', 'amount', 'kind', 'details', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const hiveCalls = await tx
        .selectFrom('hive_calls')
        .select(['id', 'reserved', 'status', 'charged', 'rate', 'usage', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const hivePurchases = await tx
        .selectFrom('hive_purchases')
        .select(['id', 'checkout_id', 'payment_id', 'pack', 'paid', 'reversed', 'created_at'])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      // Lease and run identifiers are internal capabilities, not export data.
      const hiveSessions = await tx
        .selectFrom('hive_sessions')
        .select([
          'id',
          'match_id',
          'seat',
          'team',
          'tick',
          'generation',
          'supervision',
          'pending_run',
          'last_wake_tick',
          'created_at',
        ])
        .where('account_id', '=', id)
        .orderBy('created_at')
        .execute();
      const hiveEvents = await tx
        .selectFrom('hive_events as e')
        .innerJoin('hive_sessions as s', 's.id', 'e.session_id')
        .select(['e.id', 'e.session_id', 'e.kind', 'e.body', 'e.created_at'])
        .where('s.account_id', '=', id)
        .orderBy('e.id')
        .execute();
      const hiveOperations = await tx
        .selectFrom('hive_operations as o')
        .innerJoin('hive_sessions as s', 's.id', 'o.session_id')
        .select([
          'o.id',
          'o.session_id',
          'o.generation',
          'o.status',
          'o.request',
          'o.result',
          'o.supervised',
          'o.created_at',
        ])
        .where('s.account_id', '=', id)
        .orderBy('o.created_at')
        .execute();
      const hivePrograms = await tx
        .selectFrom('hive_programs as p')
        .innerJoin('hive_sessions as s', 's.id', 'p.session_id')
        .select(['p.session_id', 'p.id', 'p.revision', 'p.definition', 'p.status', 'p.supervised'])
        .where('s.account_id', '=', id)
        .orderBy('p.id')
        .execute();

      const studioWallets = await tx
        .selectFrom('map_wallets')
        .select(['balance', 'reserved'])
        .where('account_id', '=', id)
        .execute();
      const studioLedger = await tx
        .selectFrom('map_ledger')
        .select(['id', 'amount', 'kind', 'details', 'created_at'])
        .where('account_id', '=', id)
        .execute();
      const studioCalls = await tx
        .selectFrom('map_calls')
        .select(['id', 'reserved', 'status', 'charged', 'rate', 'usage', 'created_at'])
        .where('account_id', '=', id)
        .execute();
      const studioPurchases = await tx
        .selectFrom('map_purchases')
        .select(['id', 'checkout_id', 'payment_id', 'pack', 'paid', 'reversed', 'created_at'])
        .where('account_id', '=', id)
        .execute();
      const studioThreads = await tx
        .selectFrom('studio_threads')
        .select(['id', 'brief', 'title', 'created_at', 'updated_at'])
        .where('account_id', '=', id)
        .execute();
      const studioRequests = await tx
        .selectFrom('studio_requests')
        .select([
          'id',
          'thread_id',
          'kind',
          'status',
          'input',
          'checkpoints',
          'map_id',
          'map_hash',
          'error',
          'charged',
          'created_at',
          'completed_at',
        ])
        .where('account_id', '=', id)
        .execute();
      const studioMessages = await tx
        .selectFrom('studio_messages as m')
        .innerJoin('studio_threads as t', 't.id', 'm.thread_id')
        .select(['m.id', 'm.thread_id', 'm.role', 'm.text', 'm.created_at'])
        .where('t.account_id', '=', id)
        .orderBy('m.created_at')
        .orderBy('m.id')
        .execute();
      const studioEvents = await tx
        .selectFrom('studio_events as e')
        .innerJoin('studio_threads as t', 't.id', 'e.thread_id')
        .select(['e.thread_id', 'e.cursor', 'e.request_id', 'e.type', 'e.payload', 'e.created_at'])
        .where('t.account_id', '=', id)
        .orderBy('e.cursor')
        .execute();
      const studioArtifacts = await tx
        .selectFrom('studio_artifacts as a')
        .innerJoin('studio_threads as t', 't.id', 'a.thread_id')
        .select([
          'a.id',
          'a.thread_id',
          'a.request_id',
          'a.stage',
          'a.kind',
          'a.label',
          'a.hash',
          'a.width',
          'a.height',
          'a.created_at',
        ])
        .where('t.account_id', '=', id)
        .orderBy('a.created_at')
        .execute();
      const studioAttempts = await tx
        .selectFrom('studio_attempts as a')
        .innerJoin('studio_requests as r', 'r.id', 'a.request_id')
        .select([
          'a.id',
          'a.request_id',
          'a.stage',
          'a.model',
          'a.status',
          'a.input',
          'a.output',
          'a.created_at',
        ])
        .where('r.account_id', '=', id)
        .orderBy('a.created_at')
        .execute();

      return {
        format: ACCOUNT_EXPORT_FORMAT,
        exportedAt: new Date().toISOString(),
        instance: origin,
        account: clean({
          avatar_source: account.avatar_source,
          avatar_url: `${origin}/api/v1/accounts/${account.id}/avatar`,
          id: account.id,
          kind: account.kind,
          display_name: account.display_name,
          role: account.role,
          status: account.status,
          created_at: account.created_at,
          last_seen_at: account.last_seen_at,
          display_name_changed_at: account.display_name_changed_at,
          muted_until: account.muted_until,
        }),
        signIn: {
          identities: rows(identities),
          devices: rows(devices),
          refreshTokens: rows(refreshTokens),
          webSessions: rows(webSessions),
          signInAttempts: rows(signInAttempts),
        },
        skins: {
          published: skins.map((skin) => ({
            ...clean(skin),
            versions: rows(skinVersions.filter((version) => version.skin_id === skin.id)),
          })),
          equipment: rows(skinEquipment),
          drafts: skinDrafts.map(({ image, material, ...draft }) => ({
            ...clean(draft),
            imageBase64: image.toString('base64'),
            materialBase64: material.toString('base64'),
            contentType: 'image/png',
          })),
          matches: rows(skinMatches),
          purchases: rows(skinPurchases),
          paymentEvents: rows(skinPaymentEvents),
          reports: rows(skinReports),
        },
        entitlements: rows(entitlements),
        moderation: rows(moderation),
        ratings: rows(ratings),
        ratingHistory: rows(ratingHistory),
        matches: matches.map((m) => ({
          ...clean(m),
          page: `${origin}/matches/${m.match_id}`,
        })),
        rooms: {
          hosted: rows(roomsHosted),
          memberships: rows(roomMemberships),
          seats: rows(roomSeats),
          chat: rows(roomChat),
          kicks: rows(roomKicks),
        },
        matchmaking: {
          tickets: rows(queueTickets),
          cooldowns: rows(queueCooldowns),
          proposals: rows(matchProposals),
        },
        hive: {
          wallets: rows(hiveWallets),
          ledger: rows(hiveLedger),
          calls: rows(hiveCalls),
          purchases: rows(hivePurchases),
          sessions: rows(hiveSessions),
          events: rows(hiveEvents),
          operations: rows(hiveOperations),
          programs: rows(hivePrograms),
        },
        mapStudio: {
          wallets: rows(studioWallets),
          ledger: rows(studioLedger),
          calls: rows(studioCalls),
          purchases: rows(studioPurchases),
          threads: rows(studioThreads),
          messages: rows(studioMessages),
          requests: rows(studioRequests),
          attempts: rows(studioAttempts),
          events: rows(studioEvents),
          artifacts: rows(studioArtifacts),
        },
        maps: {
          published: maps.map((m) => ({
            ...clean(m),
            versions: rows(mapVersions.filter((v) => v.map_id === m.id)).map(
              ({ mapId: _mapId, ...v }) => v,
            ),
          })),
          likes: rows(mapLikes),
          reports: rows(mapReports),
          uploads: rows(uploads),
          downloads: rows(mapDownloads),
        },
      };
    });
}
