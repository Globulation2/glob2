-- Recovery from states that used to be terminal: rooms stuck in 'starting',
-- verify jobs lost or out of retries, and matches whose verification failed.
-- Also the scheduler leader's fencing epoch and spilled NOTIFY payloads.

-- ---------------------------------------------------------------- rooms

-- When the room entered 'starting'. RoomService.sweep resumes or reverts rooms
-- left there by a crash between the start steps (apps/api play/rooms.ts).
ALTER TABLE rooms
  ADD COLUMN starting_since timestamptz,
  -- Why the last start did not happen, shown to the members (RoomState.notice).
  ADD COLUMN notice text CHECK (notice IS NULL OR char_length(notice) <= 500);
UPDATE rooms SET starting_since = updated_at WHERE status = 'starting';
CREATE INDEX rooms_starting_idx ON rooms (starting_since) WHERE status = 'starting';

-- ------------------------------------------------------- verify jobs

-- At most one verify-match job in flight per match. Older duplicates (from
-- concurrent end-report retries) are retired first, keeping the oldest.
UPDATE engine_jobs j
SET status = 'failed',
    completed_at = now(),
    error = '{"code":"internal","message":"superseded by an older verify job for the same match"}'
WHERE j.kind = 'verify-match' AND j.status = 'queued' AND j.match_id IS NOT NULL
  AND EXISTS (
    SELECT 1 FROM engine_jobs o
    WHERE o.kind = 'verify-match' AND o.status = 'queued' AND o.match_id = j.match_id
      AND (o.created_at, o.id) < (j.created_at, j.id)
  );
CREATE UNIQUE INDEX engine_jobs_one_active_verify_idx ON engine_jobs (match_id)
  WHERE kind = 'verify-match' AND status = 'queued';
-- The stale-job sweep (worker play/jobSweep.ts) reads queued jobs by age.
CREATE INDEX engine_jobs_queued_idx ON engine_jobs (created_at) WHERE status = 'queued';

-- 'failed': the verify job failed or was lost after its retries. The match is
-- not rated; an operator can re-run verification (platform matches reverify).
ALTER TABLE matches
  DROP CONSTRAINT matches_verification_check,
  ADD CONSTRAINT matches_verification_check
    CHECK (verification IN ('pending', 'verified', 'diverged', 'unverifiable', 'not_applicable', 'failed'));

-- --------------------------------------------------------------- leader

-- Fencing for the scheduler leader (packages/db leader.ts): each new leader
-- bumps the epoch; leader-only writes check it under FOR SHARE, so a stale
-- leader whose lock was lost cannot commit after a new one took over.
CREATE TABLE leader_leases (
  name text PRIMARY KEY CHECK (char_length(name) BETWEEN 1 AND 64),
  epoch bigint NOT NULL,
  holder text NOT NULL,
  acquired_at timestamptz NOT NULL DEFAULT now(),
  renewed_at timestamptz NOT NULL DEFAULT now()
);

-- -------------------------------------------------------- notifications

-- NOTIFY payloads over Postgres' 8000-byte limit (packages/db notify.ts): the
-- payload is stored here and the notification carries only its id; the
-- receiving PgPubSub reads it back. Rows are deleted after an hour.
CREATE TABLE notification_payloads (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  channel text NOT NULL,
  payload jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX notification_payloads_created_idx ON notification_payloads (created_at);
