-- Building credits share billing code, with independent balances and purchases.
CREATE TABLE building_wallets (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  balance bigint NOT NULL DEFAULT 0 CHECK (balance BETWEEN -9007199254740991 AND 9007199254740991),
  reserved bigint NOT NULL DEFAULT 0 CHECK (reserved BETWEEN 0 AND 9007199254740991)
);
CREATE TABLE building_ledger (
  id text PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  amount bigint NOT NULL CHECK (amount BETWEEN -9007199254740991 AND 9007199254740991),
  kind text NOT NULL CHECK (kind IN ('grant','purchase','usage','refund','dispute','adjustment')),
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE building_calls (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reserved bigint NOT NULL CHECK (reserved BETWEEN 1 AND 9007199254740991),
  status text NOT NULL CHECK (status IN ('reserved','dispatched','settled','uncertain')),
  charged bigint CHECK (charged BETWEEN 0 AND 9007199254740991),
  rate jsonb NOT NULL,
  usage jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE building_purchases (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  checkout_id text UNIQUE,
  payment_id text UNIQUE,
  pack jsonb NOT NULL,
  paid boolean NOT NULL DEFAULT false,
  reversed bigint NOT NULL DEFAULT 0 CHECK(reversed >= 0),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX building_ledger_account ON building_ledger(account_id, created_at);

CREATE TABLE building_studio_threads (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 brief text NOT NULL DEFAULT '' CHECK(char_length(brief)<=16000),
 title text NOT NULL CHECK(char_length(title) BETWEEN 1 AND 128),
 created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX building_studio_threads_owner ON building_studio_threads(account_id,updated_at DESC);
CREATE TABLE building_studio_messages (
 id uuid PRIMARY KEY,
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 role text NOT NULL CHECK(role IN ('user','assistant')),
 text text NOT NULL CHECK(char_length(text) BETWEEN 1 AND 16000),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX building_studio_messages_thread ON building_studio_messages(thread_id,created_at,id);
CREATE TABLE building_studio_requests (
 id uuid PRIMARY KEY,
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 kind text NOT NULL CHECK(kind IN ('chat','generate')),
 status text NOT NULL DEFAULT 'queued' CHECK(status IN ('queued','preparing','dispatched','processing','importing','ready','failed','uncertain')),
 input jsonb NOT NULL,
 checkpoints jsonb NOT NULL DEFAULT '{}',
 lease_until timestamptz,
 lease uuid,

 error text,
 charged boolean NOT NULL DEFAULT false,
 created_at timestamptz NOT NULL DEFAULT now(),
 completed_at timestamptz
);
CREATE UNIQUE INDEX building_studio_one_active ON building_studio_requests(account_id)
 WHERE status NOT IN ('ready','failed');
CREATE INDEX building_studio_requests_queue ON building_studio_requests(created_at) WHERE status='queued';
CREATE TABLE building_studio_attempts (
 id uuid PRIMARY KEY,
 request_id uuid REFERENCES building_studio_requests(id) ON DELETE CASCADE,
 stage text NOT NULL,
 model text NOT NULL,
 status text NOT NULL CHECK(status IN ('dispatched','completed','failed','uncertain')),
 input jsonb NOT NULL,
 output jsonb,
 created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(request_id,stage)
);
-- Transactional per-thread cursors serialize allocation and commit order. A global
-- sequence would allow an earlier uncommitted event to be skipped by a reader.
ALTER TABLE building_studio_threads ADD COLUMN event_cursor bigint NOT NULL DEFAULT 0 CHECK (event_cursor >= 0);
CREATE TABLE building_studio_events (
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 cursor bigint NOT NULL CHECK (cursor > 0),
 request_id uuid REFERENCES building_studio_requests(id) ON DELETE CASCADE,
 dedup text NOT NULL,
 type text NOT NULL CHECK (type IN ('state','stage','artifact','check','message','complete','text')),
 payload jsonb NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now(),
 PRIMARY KEY(thread_id,cursor),
 UNIQUE(thread_id,dedup)
);
CREATE INDEX building_studio_events_request ON building_studio_events(request_id,cursor);
CREATE TABLE building_studio_artifacts (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 request_id uuid REFERENCES building_studio_requests(id) ON DELETE CASCADE,
 stage text NOT NULL CHECK (stage IN ('prepare','artwork','assemble','checks','ready')),
 kind text NOT NULL CHECK (kind IN ('reference','preview','source','report')),
 label text NOT NULL,
 hash text NOT NULL REFERENCES blobs(sha256),
 created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(request_id,stage,kind,hash)
);
CREATE INDEX building_studio_artifacts_thread ON building_studio_artifacts(thread_id,request_id);
CREATE INDEX building_studio_artifacts_hash ON building_studio_artifacts(hash);

-- Account deletion removes the private provider journal, but must not restore
-- service capacity already spent. Retain only anonymous daily call totals.
-- A trigger counts every dispatched call independently of private history.
LOCK TABLE building_studio_attempts IN SHARE ROW EXCLUSIVE MODE;
CREATE TABLE building_studio_provider_usage (
 day date PRIMARY KEY,
 calls bigint NOT NULL CHECK(calls >= 0)
);
INSERT INTO building_studio_provider_usage(day,calls)
 SELECT (created_at AT TIME ZONE 'UTC')::date,count(*)
 FROM building_studio_attempts GROUP BY 1;
CREATE FUNCTION count_building_studio_provider_call() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 INSERT INTO building_studio_provider_usage(day,calls)
 VALUES((NEW.created_at AT TIME ZONE 'UTC')::date,1)
 ON CONFLICT(day) DO UPDATE SET calls=building_studio_provider_usage.calls+1;
 RETURN NEW;
END;
$$;
CREATE TRIGGER building_studio_provider_call_count AFTER INSERT ON building_studio_attempts
 FOR EACH ROW EXECUTE FUNCTION count_building_studio_provider_call();


ALTER TABLE building_studio_threads ADD COLUMN draft_id uuid NOT NULL REFERENCES building_drafts(id) ON DELETE CASCADE;
CREATE TABLE building_studio_revisions (
 request_id uuid PRIMARY KEY REFERENCES building_studio_requests(id) ON DELETE CASCADE,
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 base_revision uuid NOT NULL, title text NOT NULL, document jsonb NOT NULL, archive bytea NOT NULL CHECK(octet_length(archive)<=33554432),
 hash text NOT NULL REFERENCES blobs(sha256), report jsonb NOT NULL, sim_version text NOT NULL,
 applied boolean NOT NULL DEFAULT false, created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX building_studio_revisions_thread ON building_studio_revisions(thread_id,created_at);
