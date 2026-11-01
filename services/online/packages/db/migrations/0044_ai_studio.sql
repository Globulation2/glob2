-- AI Studio has a separate balance and purchase ledger.
CREATE TABLE ai_studio_wallets (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  balance bigint NOT NULL DEFAULT 0 CHECK (balance BETWEEN -9007199254740991 AND 9007199254740991),
  reserved bigint NOT NULL DEFAULT 0 CHECK (reserved BETWEEN 0 AND 9007199254740991)
);
CREATE TABLE ai_studio_ledger (
  id text PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  amount bigint NOT NULL CHECK (amount BETWEEN -9007199254740991 AND 9007199254740991),
  kind text NOT NULL CHECK (kind IN ('grant','purchase','usage','refund','dispute','adjustment')),
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE ai_studio_calls (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reserved bigint NOT NULL CHECK (reserved BETWEEN 1 AND 9007199254740991),
  status text NOT NULL CHECK (status IN ('reserved','dispatched','settled','uncertain')),
  charged bigint CHECK (charged BETWEEN 0 AND 9007199254740991),
  rate jsonb NOT NULL,
  usage jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE ai_studio_purchases (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  checkout_id text UNIQUE,
  payment_id text UNIQUE,
  pack jsonb NOT NULL,
  paid boolean NOT NULL DEFAULT false,
  reversed bigint NOT NULL DEFAULT 0 CHECK(reversed >= 0),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ai_studio_ledger_account ON ai_studio_ledger(account_id, created_at);


CREATE TABLE ai_studio_projects (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 title text NOT NULL CHECK(char_length(title) BETWEEN 1 AND 128),
 revision integer NOT NULL DEFAULT 1,
 created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ai_studio_projects_owner ON ai_studio_projects(account_id, updated_at DESC);
CREATE TABLE ai_studio_revisions (
 project_id uuid NOT NULL REFERENCES ai_studio_projects(id) ON DELETE CASCADE,
 revision integer NOT NULL,
 source text NOT NULL CHECK(octet_length(source) BETWEEN 1 AND 131072),
 hash sha256_hex NOT NULL,
 reason text NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now(),
 PRIMARY KEY(project_id, revision)
);
CREATE TABLE ai_studio_requests (
 id uuid PRIMARY KEY,
 project_id uuid NOT NULL REFERENCES ai_studio_projects(id) ON DELETE CASCADE,
 base_revision integer NOT NULL,
 prompt text NOT NULL CHECK(char_length(prompt) BETWEEN 1 AND 16000),
 diagnostics text NOT NULL DEFAULT '' CHECK(char_length(diagnostics)<=16000),
 budget integer NOT NULL CHECK(budget>0),
 status text NOT NULL DEFAULT 'queued' CHECK(status IN ('queued','running','completed','failed','cancelled','uncertain')),
 response text NOT NULL DEFAULT '',
 error text,
 cancelled boolean NOT NULL DEFAULT false,
 lease_until timestamptz,
 created_at timestamptz NOT NULL DEFAULT now(),
 FOREIGN KEY(project_id, base_revision) REFERENCES ai_studio_revisions(project_id, revision)
);
CREATE UNIQUE INDEX ai_studio_one_request ON ai_studio_requests(project_id) WHERE status IN ('queued','running');
CREATE INDEX ai_studio_dispatch ON ai_studio_requests(status,created_at,id) WHERE status IN ('queued','running');
CREATE TABLE ai_studio_events (
 id bigserial PRIMARY KEY,
 project_id uuid NOT NULL REFERENCES ai_studio_projects(id) ON DELETE CASCADE,
 request_id uuid,
 kind text NOT NULL,
 body jsonb NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ai_studio_events_project ON ai_studio_events(project_id,id);
CREATE TABLE ai_studio_runs (
 id uuid PRIMARY KEY,
 project_id uuid NOT NULL REFERENCES ai_studio_projects(id) ON DELETE CASCADE,
 revision integer NOT NULL,
 seed bigint NOT NULL CHECK(seed BETWEEN 0 AND 4294967295),
 opponent text NOT NULL CHECK(opponent IN ('numbi','nicowar')),
 summary text NOT NULL DEFAULT '' CHECK(char_length(summary)<=16000),
 created_at timestamptz NOT NULL DEFAULT now(),
 FOREIGN KEY(project_id,revision) REFERENCES ai_studio_revisions(project_id,revision)
);
ALTER TABLE ai_studio_requests ADD COLUMN provider_result jsonb;
