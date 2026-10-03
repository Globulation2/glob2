-- Map catalog (M7): versions validated and previewed by engine agents, likes,
-- reports and moderation, counted plays and downloads. Extends the maps
-- tables of 0001. See docs/multiplayer/architecture.md (map catalog).

-- ------------------------------------------------------------------- maps

ALTER TABLE maps
  -- How the map was made, as its owner declared it: drawn in the editor, or
  -- produced by a map generator (with the descriptor, when known).
  ADD COLUMN made_with text NOT NULL DEFAULT 'hand' CHECK (made_with IN ('hand', 'generator')),
  ADD COLUMN generator jsonb,
  -- Kept in step with map_likes (in the same transaction as each like).
  ADD COLUMN like_count integer NOT NULL DEFAULT 0 CHECK (like_count >= 0),
  -- The newest valid version, which listings show and filter on.
  ADD COLUMN latest_version_id uuid,
  ADD COLUMN hidden_at timestamptz,
  ADD COLUMN hidden_by_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL;

CREATE INDEX maps_public_likes_idx ON maps (like_count DESC, id DESC)
  WHERE visibility = 'public' AND NOT hidden;
CREATE INDEX maps_public_plays_idx ON maps (play_count DESC, id DESC)
  WHERE visibility = 'public' AND NOT hidden;
CREATE INDEX maps_owner_updated_idx ON maps (owner_account_id, updated_at DESC);

-- ---------------------------------------------------------- map versions

ALTER TABLE map_versions
  -- Sim version whose engine agents validate and preview the version.
  ADD COLUMN sim_version sim_version_key,
  ADD COLUMN validate_job_id uuid,
  ADD COLUMN preview_job_id uuid,
  ADD COLUMN preview_status text NOT NULL DEFAULT 'pending'
    CHECK (preview_status IN ('pending', 'ready', 'failed')),
  ADD COLUMN preview_width integer,
  ADD COLUMN preview_height integer,
  -- The map name stored in the file.
  ADD COLUMN file_title text,
  ADD COLUMN uploader_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  ADD COLUMN notes text NOT NULL DEFAULT '' CHECK (char_length(notes) <= 2000);

ALTER TABLE maps ADD CONSTRAINT maps_latest_version_fk
  FOREIGN KEY (latest_version_id) REFERENCES map_versions (id) ON DELETE SET NULL;

CREATE INDEX map_versions_map_idx ON map_versions (map_id, created_at DESC);
CREATE INDEX map_versions_validate_job_idx ON map_versions (validate_job_id);
CREATE INDEX map_versions_preview_job_idx ON map_versions (preview_job_id);
CREATE INDEX map_versions_preview_idx ON map_versions (preview_hash);

-- ------------------------------------------------------- reports, counts

-- One open report per reporter and map; resolved ones stay for the record.
CREATE UNIQUE INDEX map_reports_open_unique_idx ON map_reports (map_id, reporter_account_id)
  WHERE status = 'open';
ALTER TABLE map_reports ADD COLUMN resolution_note text
  CHECK (resolution_note IS NULL OR char_length(resolution_note) <= 2000);

-- Downloads counted once per map, downloader (account or address) and day.
CREATE TABLE map_downloads (
  map_id uuid NOT NULL REFERENCES maps (id) ON DELETE CASCADE,
  downloader text NOT NULL CHECK (char_length(downloader) <= 128),
  day date NOT NULL DEFAULT current_date,
  PRIMARY KEY (map_id, downloader, day)
);

-- Plays: matches.map_hash joined to map versions (counted at match end).
CREATE INDEX matches_map_hash_idx ON matches (map_hash);
