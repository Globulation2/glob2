-- Rooms and matches (M4): seat locks, relay round trips of room members,
-- uploaded maps and saves, generated maps, relay allocation bookkeeping and
-- match-end intake. See docs/multiplayer/architecture.md (rooms and matches).
--
-- Numbered 0005: 0004 belongs to the engine-agent work (warm map pool).

-- ----------------------------------------------------------------- rooms

-- A locked seat is closed by the host: nobody may take it, and it plays as an
-- inactive player (AI `none`) if the match starts with it empty.
ALTER TABLE room_seats ADD COLUMN locked boolean NOT NULL DEFAULT false;
ALTER TABLE room_seats ADD CONSTRAINT room_seats_locked_empty
  CHECK (NOT locked OR occupant = 'open');

-- Relay round trips a member measured ([{ "region": "eu-west", "rttMs": 32 }]),
-- used to place the room's match on the relay closest to everyone seated.
ALTER TABLE room_members ADD COLUMN region_rtts jsonb NOT NULL DEFAULT '[]';

CREATE INDEX rooms_open_idx ON rooms (updated_at) WHERE status <> 'closed';

-- ------------------------------------------------------------ map uploads

-- A map or save a player uploaded for private use in their rooms. The bytes
-- are a private blob; an engine agent of `sim_version` validates them
-- (validate-map) and reports the map facts. The uploaded bytes are the bytes
-- clients load, so a valid upload's map hash is its blob hash.
CREATE TABLE map_uploads (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  owner_account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  blob_sha256 sha256_hex NOT NULL REFERENCES blobs (sha256),
  format text NOT NULL CHECK (format IN ('map', 'save')),
  sim_version sim_version_key NOT NULL,
  file_name text CHECK (char_length(file_name) <= 255),
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'valid', 'invalid')),
  job_id uuid REFERENCES engine_jobs (id) ON DELETE SET NULL,
  width integer,
  height integer,
  team_count smallint CHECK (team_count BETWEEN 1 AND 12),
  version_minor integer,
  title text,
  -- Saves: the players recorded in the file ([{ "name", "team", "kind" }]),
  -- which the host maps to returning players (reteaming).
  players jsonb,
  failure text,
  created_at timestamptz NOT NULL DEFAULT now(),
  completed_at timestamptz,
  UNIQUE (owner_account_id, blob_sha256, format, sim_version)
);
CREATE INDEX map_uploads_job_idx ON map_uploads (job_id);

-- --------------------------------------------------------- generated maps

-- One generation per (descriptor, sim version): generation is deterministic
-- for a sim version, so rooms and matches asking for the same descriptor share
-- the result. descriptor_hash is the SHA-256 of the canonical descriptor JSON.
CREATE TABLE generated_maps (
  descriptor_hash sha256_hex NOT NULL,
  sim_version sim_version_key NOT NULL,
  descriptor jsonb NOT NULL,
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'ready', 'failed')),
  job_id uuid REFERENCES engine_jobs (id) ON DELETE SET NULL,
  map_hash sha256_hex,
  width integer,
  height integer,
  team_count smallint,
  failure text,
  created_at timestamptz NOT NULL DEFAULT now(),
  completed_at timestamptz,
  PRIMARY KEY (descriptor_hash, sim_version),
  CHECK (status <> 'ready' OR map_hash IS NOT NULL)
);
CREATE INDEX generated_maps_job_idx ON generated_maps (job_id);

-- --------------------------------------------------------------- relays

-- Healthy relays are found by heartbeat age; draining ones never get matches.
CREATE INDEX relays_available_idx ON relays (last_heartbeat_at) WHERE NOT draining;

-- -------------------------------------------------------------- matches

ALTER TABLE matches
  -- When the match was (last) placed on its relay; re-placed after a refusal.
  ADD COLUMN relay_assigned_at timestamptz,
  ADD COLUMN relay_attempts integer NOT NULL DEFAULT 0,
  -- The relay's RelayMatchEnded report, exactly as received.
  ADD COLUMN end_report jsonb;

CREATE INDEX matches_relay_idx ON matches (relay_id) WHERE status IN ('starting', 'running');
CREATE INDEX matches_room_idx ON matches (room_id);
