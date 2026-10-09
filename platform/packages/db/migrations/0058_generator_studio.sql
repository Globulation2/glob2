-- Generator Studio has a separate balance and purchase ledger.
CREATE TABLE generator_studio_wallets (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  balance bigint NOT NULL DEFAULT 0 CHECK (balance BETWEEN -9007199254740991 AND 9007199254740991),
  reserved bigint NOT NULL DEFAULT 0 CHECK (reserved BETWEEN 0 AND 9007199254740991)
);
CREATE TABLE generator_studio_ledger (
  id text PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  amount bigint NOT NULL CHECK (amount BETWEEN -9007199254740991 AND 9007199254740991),
  kind text NOT NULL CHECK (kind IN ('grant','purchase','usage','refund','dispute','adjustment')),
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE generator_studio_calls (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reserved bigint NOT NULL CHECK (reserved BETWEEN 1 AND 9007199254740991),
  status text NOT NULL CHECK (status IN ('reserved','dispatched','settled','uncertain')),
  charged bigint CHECK (charged BETWEEN 0 AND 9007199254740991),
  rate jsonb NOT NULL,
  usage jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE generator_studio_purchases (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  checkout_id text UNIQUE,
  payment_id text UNIQUE,
  pack jsonb NOT NULL,
  paid boolean NOT NULL DEFAULT false,
  reversed bigint NOT NULL DEFAULT 0 CHECK(reversed >= 0),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX generator_studio_ledger_account ON generator_studio_ledger(account_id, created_at);

CREATE TABLE generator_studio_projects (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  title text NOT NULL CHECK(char_length(title) BETWEEN 1 AND 128),
  revision integer NOT NULL DEFAULT 1,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX generator_studio_projects_owner ON generator_studio_projects(account_id, updated_at DESC);
CREATE TABLE generator_studio_revisions (
  project_id uuid NOT NULL REFERENCES generator_studio_projects(id) ON DELETE CASCADE,
  revision integer NOT NULL,
  source text NOT NULL CHECK(octet_length(source) BETWEEN 1 AND 262144),
  hash sha256_hex NOT NULL,
  reason text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(project_id, revision)
);
CREATE TABLE generator_studio_requests (
  id uuid PRIMARY KEY,
  project_id uuid NOT NULL REFERENCES generator_studio_projects(id) ON DELETE CASCADE,
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
  FOREIGN KEY(project_id, base_revision) REFERENCES generator_studio_revisions(project_id, revision)
);
CREATE UNIQUE INDEX generator_studio_one_request ON generator_studio_requests(project_id) WHERE status IN ('queued','running');
CREATE INDEX generator_studio_dispatch ON generator_studio_requests(status,created_at,id) WHERE status IN ('queued','running');
CREATE TABLE generator_studio_events (
  id bigserial PRIMARY KEY,
  project_id uuid NOT NULL REFERENCES generator_studio_projects(id) ON DELETE CASCADE,
  request_id uuid,
  kind text NOT NULL,
  body jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX generator_studio_events_project ON generator_studio_events(project_id,id);
CREATE TABLE generator_studio_runs (
  id uuid PRIMARY KEY,
  project_id uuid NOT NULL REFERENCES generator_studio_projects(id) ON DELETE CASCADE,
  revision integer NOT NULL,
  settings jsonb NOT NULL,
  source_hash sha256_hex NOT NULL,
  summary text NOT NULL DEFAULT '' CHECK(char_length(summary)<=16000),
  created_at timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY(project_id,revision) REFERENCES generator_studio_revisions(project_id,revision)
);
CREATE TABLE generator_studio_checks (
  project_id uuid NOT NULL REFERENCES generator_studio_projects(id) ON DELETE CASCADE,
  revision integer NOT NULL,
  upload_id uuid NOT NULL REFERENCES generator_uploads(id) ON DELETE CASCADE,
  PRIMARY KEY(project_id,revision,upload_id),
  FOREIGN KEY(project_id,revision) REFERENCES generator_studio_revisions(project_id,revision)
);
ALTER TABLE generator_studio_requests ADD COLUMN provider_result jsonb;

ALTER TABLE generator_studio_calls ADD COLUMN completed_at timestamptz;

ALTER TABLE generator_studio_requests ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON generator_studio_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON generator_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON generator_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('generatorStudio');
CREATE TRIGGER admin_forget_metric AFTER DELETE ON generator_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_forget_metric_source('generatorStudio');
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON generator_studio_calls FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('generatorStudio');
INSERT INTO admin_metric_coverage VALUES ('status.generatorStudio',current_date,true),('duration.generatorStudio',current_date,true);
CREATE INDEX generator_studio_ledger_admin_period_idx ON generator_studio_ledger(created_at,kind);
CREATE INDEX generator_studio_calls_admin_returned_idx ON generator_studio_calls(completed_at) WHERE status='settled';
CREATE INDEX generator_studio_calls_admin_uncertain_idx ON generator_studio_calls(created_at,id) WHERE status='uncertain';
