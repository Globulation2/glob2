-- Retention (worker maintenance.ts; policy in docs/hosting/README.md,
-- "Retention"): refresh-token rotation with a reuse grace, and the indexes the
-- cleanup deletes need so they do not scan whole tables.

-- ------------------------------------------------------- refresh tokens

ALTER TABLE refresh_tokens
  -- The token that replaced this one when it was rotated. A concurrent refresh
  -- with this (immediately previous) token inside the reuse grace gets a new
  -- token instead of revoking the family.
  ADD COLUMN replaced_by uuid,
  -- Refreshes accepted inside the grace after the first rotation (bounded).
  ADD COLUMN grace_uses smallint NOT NULL DEFAULT 0;
CREATE INDEX refresh_tokens_expires_idx ON refresh_tokens (expires_at);
CREATE INDEX refresh_tokens_rotated_idx ON refresh_tokens (rotated_at) WHERE rotated_at IS NOT NULL;
CREATE INDEX refresh_tokens_revoked_idx ON refresh_tokens (revoked_at) WHERE revoked_at IS NOT NULL;

-- ------------------------------------------------------------ cleanup

CREATE INDEX signin_attempts_created_idx ON signin_attempts (created_at);
CREATE INDEX room_chat_messages_sent_idx ON room_chat_messages (sent_at);
CREATE INDEX rooms_closed_idx ON rooms (closed_at) WHERE status = 'closed';
CREATE INDEX engine_jobs_completed_idx ON engine_jobs (completed_at) WHERE status <> 'queued';
CREATE INDEX match_proposals_resolved_idx ON match_proposals (resolved_at)
  WHERE status IN ('started', 'cancelled', 'failed');
CREATE INDEX queue_tickets_done_idx ON queue_tickets (updated_at)
  WHERE status IN ('matched', 'cancelled', 'declined', 'expired');
CREATE INDEX accounts_guests_idx ON accounts (COALESCE(last_seen_at, created_at))
  WHERE kind = 'guest';
CREATE INDEX blobs_created_idx ON blobs (created_at);
-- Blob references that have no foreign key (garbage collection checks them).
CREATE INDEX generated_maps_map_hash_idx ON generated_maps (map_hash) WHERE map_hash IS NOT NULL;
CREATE INDEX warm_maps_map_hash_idx ON warm_maps (map_hash) WHERE map_hash IS NOT NULL;
CREATE INDEX map_uploads_blob_idx ON map_uploads (blob_sha256);
CREATE INDEX match_artifacts_blob_idx ON match_artifacts (blob_sha256);
