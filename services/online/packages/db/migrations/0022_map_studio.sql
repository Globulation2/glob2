-- Map credits share billing code, never Hive balances or purchases.
CREATE TABLE map_wallets (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  balance bigint NOT NULL DEFAULT 0 CHECK (balance BETWEEN -9007199254740991 AND 9007199254740991),
  reserved bigint NOT NULL DEFAULT 0 CHECK (reserved BETWEEN 0 AND 9007199254740991)
);
CREATE TABLE map_ledger (
  id text PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  amount bigint NOT NULL CHECK (amount BETWEEN -9007199254740991 AND 9007199254740991),
  kind text NOT NULL CHECK (kind IN ('grant','purchase','usage','refund','dispute','adjustment')),
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE map_calls (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reserved bigint NOT NULL CHECK (reserved BETWEEN 1 AND 9007199254740991),
  status text NOT NULL CHECK (status IN ('reserved','dispatched','settled','uncertain')),
  charged bigint CHECK (charged BETWEEN 0 AND 9007199254740991),
  rate jsonb NOT NULL,
  usage jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE map_purchases (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  checkout_id text UNIQUE,
  payment_id text UNIQUE,
  pack jsonb NOT NULL,
  paid boolean NOT NULL DEFAULT false,
  reversed bigint NOT NULL DEFAULT 0 CHECK(reversed >= 0),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX map_ledger_account ON map_ledger(account_id, created_at);

CREATE TABLE studio_threads (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 brief text NOT NULL DEFAULT '' CHECK(char_length(brief)<=16000),
 title text NOT NULL CHECK(char_length(title) BETWEEN 1 AND 128),
 created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX studio_threads_owner ON studio_threads(account_id,updated_at DESC);
CREATE TABLE studio_messages (
 id uuid PRIMARY KEY,
 thread_id uuid NOT NULL REFERENCES studio_threads(id) ON DELETE CASCADE,
 role text NOT NULL CHECK(role IN ('user','assistant')),
 text text NOT NULL CHECK(char_length(text) BETWEEN 1 AND 16000),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX studio_messages_thread ON studio_messages(thread_id,created_at,id);
CREATE TABLE studio_requests (
 id uuid PRIMARY KEY,
 thread_id uuid NOT NULL REFERENCES studio_threads(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 kind text NOT NULL CHECK(kind IN ('chat','generate')),
 status text NOT NULL DEFAULT 'queued' CHECK(status IN ('queued','preparing','dispatched','processing','importing','ready','failed','uncertain')),
 input jsonb NOT NULL,
 checkpoints jsonb NOT NULL DEFAULT '{}',
 lease_until timestamptz,
 lease uuid,
 map_id uuid REFERENCES maps(id) ON DELETE SET NULL,
 map_hash sha256_hex REFERENCES blobs(sha256),
 error text,
 charged boolean NOT NULL DEFAULT false,
 created_at timestamptz NOT NULL DEFAULT now(),
 completed_at timestamptz
);
CREATE UNIQUE INDEX studio_one_active ON studio_requests(account_id)
 WHERE status NOT IN ('ready','failed');
CREATE INDEX studio_requests_queue ON studio_requests(created_at) WHERE status='queued';
CREATE TABLE studio_attempts (
 id uuid PRIMARY KEY,
 request_id uuid NOT NULL REFERENCES studio_requests(id) ON DELETE CASCADE,
 stage text NOT NULL,
 model text NOT NULL,
 status text NOT NULL CHECK(status IN ('dispatched','completed','failed','uncertain')),
 input jsonb NOT NULL,
 output jsonb,
 created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(request_id,stage)
);
ALTER TABLE maps ADD COLUMN authoring jsonb;

ALTER TABLE engine_jobs DROP CONSTRAINT engine_jobs_kind_check;
ALTER TABLE engine_jobs ADD CHECK(kind IN ('generate-map','validate-map','render-preview','verify-match','import-ai-map'));
