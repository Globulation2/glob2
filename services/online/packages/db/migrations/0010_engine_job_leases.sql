-- Engine jobs leased over HTTP. Engine agents run the legacy C++ loader on
-- uploaded files, so they hold no database credentials: they lease jobs from
-- platform-api (/internal/v1/engine), which records the lease here. The
-- engine_jobs row is the queue; graphile-worker carries only the result
-- (platform:engine-job-result) to apps/worker.
--
-- attempts       leases handed out so far (a retry is a new lease)
-- max_attempts   retry budget; after the last lease expires without a report
--                the worker fails the job (failAbandonedEngineJobs)
-- lease_*        current holder, the SHA-256 of its lease token, and expiry
-- reported_at    the result was received and handed to the worker; the job
--                is no longer leasable
ALTER TABLE engine_jobs
  ADD COLUMN attempts integer NOT NULL DEFAULT 0 CHECK (attempts >= 0),
  ADD COLUMN max_attempts integer NOT NULL DEFAULT 3 CHECK (max_attempts BETWEEN 1 AND 25),
  ADD COLUMN leased_by text,
  ADD COLUMN lease_token_hash text,
  ADD COLUMN lease_expires_at timestamptz,
  ADD COLUMN reported_at timestamptz;

CREATE INDEX engine_jobs_lease_idx ON engine_jobs (sim_version, created_at)
  WHERE status = 'queued' AND reported_at IS NULL;

-- Upgrade from graphile-carried engine jobs: queued rows are now leased from
-- this table, so drop their old queue entries (no agent runs those task
-- identifiers any more). Queued rows that nothing waits for any longer (map
-- jobs older than an hour; their callers time out much sooner) are closed;
-- queued verify-match rows stay and are run again, which recovers matches
-- whose verification was lost.
DO $$
BEGIN
  IF to_regclass('graphile_worker._private_jobs') IS NOT NULL THEN
    DELETE FROM graphile_worker._private_jobs j
      USING graphile_worker._private_tasks t
      WHERE t.id = j.task_id AND t.identifier LIKE 'engine:%';
  END IF;
END $$;

UPDATE engine_jobs
SET status = 'failed',
    error = '{"code":"internal","message":"abandoned during the engine-agent upgrade"}',
    completed_at = now()
WHERE status = 'queued' AND kind <> 'verify-match' AND created_at < now() - interval '1 hour';
