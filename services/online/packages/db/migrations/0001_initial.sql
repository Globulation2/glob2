-- Initial platform data model. Migrations are forward-only plain SQL, applied
-- in name order by `glob2-migrate` (packages/db/src/migrate.ts) inside one
-- transaction each. Never edit an applied migration; add a new file.
--
-- Conventions: uuid keys from gen_random_uuid(); text + CHECK instead of enum
-- types (easier to extend); timestamptz everywhere; SHA-256 digests as
-- lowercase hex text, matching the protocol package; sim versions as the
-- protocol's simVersionKey() string.

CREATE DOMAIN sha256_hex AS text CHECK (VALUE ~ '^[0-9a-f]{64}$');
CREATE DOMAIN sim_version_key AS text CHECK (VALUE ~ '^[0-9]{1,5}-[0-9]{1,5}-[0-9a-f]{64}$');

-- ------------------------------------------------------------ identity

CREATE TABLE accounts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  kind text NOT NULL CHECK (kind IN ('guest', 'registered')),
  display_name text NOT NULL CHECK (char_length(display_name) BETWEEN 1 AND 32),
  role text NOT NULL DEFAULT 'user' CHECK (role IN ('user', 'moderator', 'admin')),
  status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'banned', 'deleted')),
  muted_until timestamptz,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz
);
CREATE INDEX accounts_display_name_idx ON accounts (lower(display_name));

-- A sign-in method linked to an account: an OIDC/Apple subject, or a local
-- username with an argon2id password hash.
CREATE TABLE identities (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  provider text NOT NULL CHECK (char_length(provider) BETWEEN 1 AND 64),
  subject text NOT NULL CHECK (char_length(subject) BETWEEN 1 AND 255),
  email text,
  password_hash text,
  created_at timestamptz NOT NULL DEFAULT now(),
  last_used_at timestamptz,
  UNIQUE (provider, subject)
);
CREATE INDEX identities_account_idx ON identities (account_id);

-- 256-bit guest device credentials, stored as SHA-256 of the secret.
CREATE TABLE device_credentials (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  credential_hash sha256_hex NOT NULL UNIQUE,
  platform text NOT NULL CHECK (platform IN ('desktop', 'android', 'ios', 'browser')),
  created_at timestamptz NOT NULL DEFAULT now(),
  last_used_at timestamptz,
  revoked_at timestamptz
);
CREATE INDEX device_credentials_account_idx ON device_credentials (account_id);

-- Rotating refresh tokens. A family is one sign-in; reuse of a rotated token
-- revokes the whole family.
CREATE TABLE refresh_tokens (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  family_id uuid NOT NULL,
  token_hash sha256_hex NOT NULL UNIQUE,
  client_platform text,
  issued_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  rotated_at timestamptz,
  revoked_at timestamptz
);
CREATE INDEX refresh_tokens_account_idx ON refresh_tokens (account_id);
CREATE INDEX refresh_tokens_family_idx ON refresh_tokens (family_id);

-- Browser sign-in handoff: a client socket starts an attempt, the user signs in
-- on the web, the platform pushes the session to the waiting socket.
CREATE TABLE signin_attempts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  confirmation_code text NOT NULL,
  provider text,
  status text NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending', 'completed', 'failed', 'cancelled', 'expired')),
  requesting_account_id uuid REFERENCES accounts (id) ON DELETE CASCADE,
  account_id uuid REFERENCES accounts (id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  completed_at timestamptz
);
CREATE INDEX signin_attempts_pending_idx ON signin_attempts (expires_at) WHERE status = 'pending';

-- Paywall hook: exists, stays empty. AccessPolicy implementations may read it.
CREATE TABLE entitlements (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  entitlement text NOT NULL CHECK (char_length(entitlement) BETWEEN 1 AND 64),
  source text NOT NULL,
  granted_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz,
  revoked_at timestamptz
);
CREATE INDEX entitlements_account_idx ON entitlements (account_id);

CREATE TABLE admin_audit_log (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  actor_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  action text NOT NULL,
  target_type text NOT NULL,
  target_id text NOT NULL,
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX admin_audit_log_target_idx ON admin_audit_log (target_type, target_id);

-- ------------------------------------------------------- infrastructure

-- Content-addressed blobs (maps, saves, previews, match records, replays,
-- result.json). The bytes live in the blob store under storage_key.
CREATE TABLE blobs (
  sha256 sha256_hex PRIMARY KEY,
  size bigint NOT NULL CHECK (size >= 0),
  content_type text NOT NULL,
  storage_key text NOT NULL,
  visibility text NOT NULL DEFAULT 'private' CHECK (visibility IN ('public', 'private')),
  owner_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE relays (
  id text PRIMARY KEY CHECK (id ~ '^[A-Za-z0-9._-]{1,64}$'),
  public_url text NOT NULL,
  region text NOT NULL,
  build text NOT NULL,
  turn_protocol integer NOT NULL,
  max_matches integer NOT NULL CHECK (max_matches > 0),
  active_matches integer NOT NULL DEFAULT 0,
  connections integer NOT NULL DEFAULT 0,
  cpu real,
  draining boolean NOT NULL DEFAULT false,
  registered_at timestamptz NOT NULL DEFAULT now(),
  last_heartbeat_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX relays_region_idx ON relays (region, last_heartbeat_at);

-- Engine agents announce which sim version (and job kinds) they serve, so the
-- platform knows which versions it can generate, preview and verify.
CREATE TABLE engine_agents (
  id text PRIMARY KEY,
  sim_version sim_version_key NOT NULL,
  kinds text[] NOT NULL,
  build text NOT NULL,
  started_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX engine_agents_sim_version_idx ON engine_agents (sim_version, last_seen_at);

-- Bookkeeping for engine jobs (the queue itself is graphile-worker's).
CREATE TABLE engine_jobs (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  kind text NOT NULL CHECK (kind IN ('generate-map', 'validate-map', 'render-preview', 'verify-match')),
  sim_version sim_version_key NOT NULL,
  payload jsonb NOT NULL,
  status text NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'succeeded', 'failed')),
  result jsonb,
  error jsonb,
  agent_id text,
  created_at timestamptz NOT NULL DEFAULT now(),
  completed_at timestamptz
);
CREATE INDEX engine_jobs_status_idx ON engine_jobs (status, created_at);

-- -------------------------------------------------------------- maps

CREATE TABLE maps (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  owner_account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  title text NOT NULL CHECK (char_length(title) BETWEEN 1 AND 128),
  description text NOT NULL DEFAULT '' CHECK (char_length(description) <= 4000),
  visibility text NOT NULL CHECK (visibility IN ('public', 'unlisted', 'private')),
  hidden boolean NOT NULL DEFAULT false,
  hidden_reason text,
  play_count integer NOT NULL DEFAULT 0,
  download_count integer NOT NULL DEFAULT 0,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX maps_owner_idx ON maps (owner_account_id);
CREATE INDEX maps_public_idx ON maps (updated_at DESC) WHERE visibility = 'public' AND NOT hidden;

CREATE TABLE map_versions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  map_id uuid NOT NULL REFERENCES maps (id) ON DELETE CASCADE,
  hash sha256_hex NOT NULL REFERENCES blobs (sha256),
  size bigint NOT NULL,
  width integer,
  height integer,
  team_count smallint CHECK (team_count BETWEEN 1 AND 12),
  min_version_minor integer,
  preview_hash sha256_hex REFERENCES blobs (sha256),
  validation text NOT NULL DEFAULT 'pending' CHECK (validation IN ('pending', 'valid', 'invalid')),
  validation_error text,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (map_id, hash)
);
CREATE INDEX map_versions_hash_idx ON map_versions (hash);

CREATE TABLE map_likes (
  map_id uuid NOT NULL REFERENCES maps (id) ON DELETE CASCADE,
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (map_id, account_id)
);

CREATE TABLE map_reports (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  map_id uuid NOT NULL REFERENCES maps (id) ON DELETE CASCADE,
  reporter_account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  reason text NOT NULL CHECK (reason IN ('broken', 'offensive', 'copyright', 'other')),
  details text NOT NULL DEFAULT '',
  status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved', 'dismissed')),
  resolved_by_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  resolved_at timestamptz
);
CREATE INDEX map_reports_open_idx ON map_reports (created_at) WHERE status = 'open';

-- ------------------------------------------------------- rooms, matches

CREATE TABLE rooms (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code text NOT NULL UNIQUE CHECK (code ~ '^[A-Za-z0-9]{6,16}$'),
  name text NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
  visibility text NOT NULL CHECK (visibility IN ('public', 'link')),
  status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'starting', 'in_match', 'closed')),
  host_account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  sim_version sim_version_key NOT NULL,
  -- Map selection, teams, rules and experiments (protocol RoomState fields).
  settings jsonb NOT NULL,
  revision integer NOT NULL DEFAULT 0,
  match_id uuid,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  closed_at timestamptz
);
CREATE INDEX rooms_listing_idx ON rooms (sim_version, updated_at DESC)
  WHERE status = 'open' AND visibility = 'public';

CREATE TABLE room_members (
  room_id uuid NOT NULL REFERENCES rooms (id) ON DELETE CASCADE,
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  connected boolean NOT NULL DEFAULT true,
  joined_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (room_id, account_id)
);
CREATE INDEX room_members_account_idx ON room_members (account_id);

CREATE TABLE room_seats (
  room_id uuid NOT NULL REFERENCES rooms (id) ON DELETE CASCADE,
  seat smallint NOT NULL CHECK (seat BETWEEN 0 AND 11),
  team smallint NOT NULL CHECK (team BETWEEN 0 AND 11),
  occupant text NOT NULL DEFAULT 'open' CHECK (occupant IN ('open', 'human', 'ai')),
  account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  ai_id text,
  ai_name text,
  ready boolean NOT NULL DEFAULT false,
  PRIMARY KEY (room_id, seat),
  UNIQUE (room_id, account_id),
  CHECK (
    (occupant = 'open' AND account_id IS NULL AND ai_id IS NULL)
    OR (occupant = 'human' AND account_id IS NOT NULL AND ai_id IS NULL)
    OR (occupant = 'ai' AND account_id IS NULL AND ai_id IS NOT NULL)
  )
);

CREATE TABLE room_chat_messages (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  room_id uuid NOT NULL REFERENCES rooms (id) ON DELETE CASCADE,
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  text text NOT NULL CHECK (char_length(text) BETWEEN 1 AND 500),
  sent_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX room_chat_messages_room_idx ON room_chat_messages (room_id, sent_at);

CREATE TABLE matches (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  sim_version sim_version_key NOT NULL,
  origin text NOT NULL CHECK (origin IN ('room', 'queue')),
  room_id uuid REFERENCES rooms (id) ON DELETE SET NULL,
  queue_id text,
  rated boolean NOT NULL DEFAULT false,
  status text NOT NULL DEFAULT 'starting'
    CHECK (status IN ('starting', 'running', 'ended', 'cancelled')),
  verification text NOT NULL DEFAULT 'pending'
    CHECK (verification IN ('pending', 'verified', 'diverged', 'unverifiable', 'not_applicable')),
  -- The protocol MatchSetup document, exactly as sent to clients and verifier.
  setup jsonb NOT NULL,
  seed bigint NOT NULL CHECK (seed BETWEEN 0 AND 4294967295),
  map_hash sha256_hex NOT NULL,
  relay_id text REFERENCES relays (id) ON DELETE SET NULL,
  end_reason text CHECK (end_reason IN ('completed', 'abandoned', 'aborted')),
  final_tick integer,
  desync_flagged boolean NOT NULL DEFAULT false,
  created_at timestamptz NOT NULL DEFAULT now(),
  started_at timestamptz,
  ended_at timestamptz,
  CHECK ((origin = 'queue') = (queue_id IS NOT NULL))
);
CREATE INDEX matches_status_idx ON matches (status) WHERE status IN ('starting', 'running');
CREATE INDEX matches_recent_idx ON matches (sim_version, created_at DESC);
CREATE INDEX matches_queue_idx ON matches (queue_id, ended_at DESC) WHERE queue_id IS NOT NULL;

ALTER TABLE rooms ADD CONSTRAINT rooms_match_fk
  FOREIGN KEY (match_id) REFERENCES matches (id) ON DELETE SET NULL;

-- -------------------------------------------------------------- ratings

-- Who can hold a rating: an account, or an AI implementation at one sim
-- version (AI revisions are never combined).
CREATE TABLE rating_entities (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  kind text NOT NULL CHECK (kind IN ('account', 'ai')),
  account_id uuid UNIQUE REFERENCES accounts (id) ON DELETE CASCADE,
  ai_id text,
  ai_sim_version sim_version_key,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (ai_id, ai_sim_version),
  CHECK (
    (kind = 'account' AND account_id IS NOT NULL AND ai_id IS NULL AND ai_sim_version IS NULL)
    OR (kind = 'ai' AND account_id IS NULL AND ai_id IS NOT NULL AND ai_sim_version IS NOT NULL)
  )
);

-- OpenSkill (Weng-Lin) rating per entity per ladder (queue id).
CREATE TABLE ratings (
  entity_id uuid NOT NULL REFERENCES rating_entities (id) ON DELETE CASCADE,
  ladder text NOT NULL,
  mu double precision NOT NULL,
  sigma double precision NOT NULL CHECK (sigma > 0),
  ordinal double precision GENERATED ALWAYS AS (mu - 3 * sigma) STORED,
  games integer NOT NULL DEFAULT 0,
  wins integer NOT NULL DEFAULT 0,
  last_match_id uuid REFERENCES matches (id) ON DELETE SET NULL,
  updated_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (entity_id, ladder)
);
CREATE INDEX ratings_ladder_idx ON ratings (ladder, ordinal DESC);

CREATE TABLE match_participants (
  match_id uuid NOT NULL REFERENCES matches (id) ON DELETE CASCADE,
  seat smallint NOT NULL CHECK (seat BETWEEN 0 AND 11),
  team smallint NOT NULL CHECK (team BETWEEN 0 AND 11),
  kind text NOT NULL CHECK (kind IN ('human', 'ai')),
  account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  ai_id text,
  rating_entity_id uuid REFERENCES rating_entities (id) ON DELETE SET NULL,
  display_name text NOT NULL,
  outcome text CHECK (outcome IN ('won', 'lost', 'unresolved', 'abandoned')),
  disconnects integer NOT NULL DEFAULT 0,
  quit_tick integer,
  rating_before double precision,
  rating_after double precision,
  PRIMARY KEY (match_id, seat),
  CHECK ((kind = 'ai') = (ai_id IS NOT NULL))
);
CREATE INDEX match_participants_account_idx ON match_participants (account_id, match_id);
CREATE INDEX match_participants_entity_idx ON match_participants (rating_entity_id);

CREATE TABLE match_team_stats (
  match_id uuid NOT NULL REFERENCES matches (id) ON DELETE CASCADE,
  team smallint NOT NULL CHECK (team BETWEEN 0 AND 11),
  outcome text NOT NULL CHECK (outcome IN ('won', 'lost', 'unresolved', 'abandoned')),
  prestige integer NOT NULL DEFAULT 0,
  eliminated_tick integer,
  statistics jsonb NOT NULL DEFAULT '{}',
  timeline jsonb NOT NULL DEFAULT '[]',
  PRIMARY KEY (match_id, team)
);

CREATE TABLE match_artifacts (
  match_id uuid NOT NULL REFERENCES matches (id) ON DELETE CASCADE,
  kind text NOT NULL CHECK (kind IN ('record', 'replay', 'result')),
  blob_sha256 sha256_hex NOT NULL REFERENCES blobs (sha256),
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (match_id, kind)
);

-- ---------------------------------------------------------------- queue

CREATE TABLE queue_tickets (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  queue_id text NOT NULL,
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  sim_version sim_version_key NOT NULL,
  -- [{ "region": "eu-west", "rttMs": 32 }, ...]
  region_rtts jsonb NOT NULL DEFAULT '[]',
  rating_mu double precision,
  rating_sigma double precision,
  status text NOT NULL DEFAULT 'waiting'
    CHECK (status IN ('waiting', 'matched', 'cancelled', 'expired')),
  match_id uuid REFERENCES matches (id) ON DELETE SET NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX queue_tickets_one_waiting_idx ON queue_tickets (account_id) WHERE status = 'waiting';
CREATE INDEX queue_tickets_waiting_idx ON queue_tickets (queue_id, sim_version, created_at)
  WHERE status = 'waiting';
