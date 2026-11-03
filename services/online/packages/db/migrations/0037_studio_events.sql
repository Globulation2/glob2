-- Transactional per-thread cursors serialize allocation and commit order. A global
-- sequence would allow an earlier uncommitted event to be skipped by a reader.
ALTER TABLE studio_threads ADD COLUMN event_cursor bigint NOT NULL DEFAULT 0 CHECK (event_cursor >= 0);
CREATE TABLE studio_events (
 thread_id uuid NOT NULL REFERENCES studio_threads(id) ON DELETE CASCADE,
 cursor bigint NOT NULL CHECK (cursor > 0),
 request_id uuid REFERENCES studio_requests(id) ON DELETE CASCADE,
 dedup text NOT NULL,
 type text NOT NULL CHECK (type IN ('state','stage','artifact','check','message','complete')),
 payload jsonb NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now(),
 PRIMARY KEY(thread_id,cursor),
 UNIQUE(thread_id,dedup)
);
CREATE INDEX studio_events_request ON studio_events(request_id,cursor);
CREATE TABLE studio_artifacts (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 thread_id uuid NOT NULL REFERENCES studio_threads(id) ON DELETE CASCADE,
 request_id uuid NOT NULL REFERENCES studio_requests(id) ON DELETE CASCADE,
 stage text NOT NULL CHECK (stage IN ('prepare','terrain','build','checks','ready')),
 kind text NOT NULL CHECK (kind IN ('reference','generated','crop','categorical','preview')),
 label text NOT NULL,
 hash text NOT NULL REFERENCES blobs(sha256),
 width integer CHECK(width > 0),
 height integer CHECK(height > 0),
 created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(request_id,stage,kind,hash)
);
CREATE INDEX studio_artifacts_thread ON studio_artifacts(thread_id,request_id);
CREATE INDEX studio_artifacts_hash ON studio_artifacts(hash);

-- Account deletion removes the private provider journal, but must not restore
-- service capacity already spent. Retain only anonymous daily call totals.
-- A trigger also accounts for workers from before this additive migration.
LOCK TABLE studio_attempts IN SHARE ROW EXCLUSIVE MODE;
CREATE TABLE studio_provider_usage (
 day date PRIMARY KEY,
 calls bigint NOT NULL CHECK(calls >= 0)
);
INSERT INTO studio_provider_usage(day,calls)
 SELECT (created_at AT TIME ZONE 'UTC')::date,count(*)
 FROM studio_attempts GROUP BY 1;
CREATE FUNCTION count_studio_provider_call() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 INSERT INTO studio_provider_usage(day,calls)
 VALUES((NEW.created_at AT TIME ZONE 'UTC')::date,1)
 ON CONFLICT(day) DO UPDATE SET calls=studio_provider_usage.calls+1;
 RETURN NEW;
END;
$$;
CREATE TRIGGER studio_provider_call_count AFTER INSERT ON studio_attempts
 FOR EACH ROW EXECUTE FUNCTION count_studio_provider_call();
