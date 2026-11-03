-- Public directory search and account-owned avatar preferences.
CREATE EXTENSION IF NOT EXISTS pg_trgm;
CREATE INDEX accounts_directory_name ON accounts (lower(display_name), id)
  WHERE kind = 'registered' AND status = 'active';
CREATE INDEX accounts_directory_search ON accounts USING gin (lower(display_name) gin_trgm_ops)
  WHERE kind = 'registered' AND status = 'active';
ALTER TABLE accounts ADD COLUMN avatar_source text NOT NULL DEFAULT 'automatic'
  CHECK (avatar_source IN ('automatic', 'uploaded', 'initials'));
ALTER TABLE accounts ADD COLUMN avatar_key text;
ALTER TABLE accounts ADD COLUMN avatar_revision integer NOT NULL DEFAULT 0;
ALTER TABLE accounts ADD COLUMN gravatar_fingerprint text;
ALTER TABLE accounts ADD COLUMN gravatar_checked_at timestamptz;
ALTER TABLE accounts ADD COLUMN gravatar_key text;
CREATE INDEX match_participants_ai_history ON match_participants (ai_id, match_id)
  WHERE kind = 'ai';
CREATE INDEX accounts_avatar_key ON accounts (avatar_key) WHERE avatar_key IS NOT NULL;
CREATE INDEX accounts_gravatar_key ON accounts (gravatar_key) WHERE gravatar_key IS NOT NULL;
