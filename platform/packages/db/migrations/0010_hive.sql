-- Hive Mind is optional. All state here is outside authoritative simulation saves.
CREATE TABLE hive_wallets (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  balance bigint NOT NULL DEFAULT 0 CHECK (balance BETWEEN -9007199254740991 AND 9007199254740991),
  reserved bigint NOT NULL DEFAULT 0 CHECK (reserved BETWEEN 0 AND 9007199254740991)
);
CREATE TABLE hive_ledger (
  id text PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  amount bigint NOT NULL CHECK (amount BETWEEN -9007199254740991 AND 9007199254740991),
  kind text NOT NULL CHECK (kind IN ('grant','purchase','usage','refund','dispute','adjustment')),
  details jsonb NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX hive_ledger_account ON hive_ledger(account_id, created_at);
CREATE TABLE hive_calls (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reserved bigint NOT NULL CHECK (reserved BETWEEN 1 AND 9007199254740991),
  status text NOT NULL CHECK (status IN ('reserved','dispatched','settled','uncertain')),
  charged bigint CHECK (charged BETWEEN 0 AND 9007199254740991),
  rate jsonb NOT NULL,
  usage jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE hive_sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  match_id uuid NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  seat integer NOT NULL CHECK (seat BETWEEN 0 AND 31),
  team integer NOT NULL CHECK (team BETWEEN 0 AND 31),
  client_id uuid,
  lease uuid,
  lease_until timestamptz,
  tick bigint NOT NULL DEFAULT 0 CHECK (tick BETWEEN 0 AND 4294967295),
  generation integer NOT NULL DEFAULT 0,
  supervision boolean NOT NULL DEFAULT false,
  pending_run boolean NOT NULL DEFAULT false,
  run_id uuid,
 run_until timestamptz,
  last_wake_tick bigint,
  wake_window timestamptz,
  wake_count integer NOT NULL DEFAULT 0,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(account_id, match_id, seat)
);
CREATE TABLE hive_events (
  id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  session_id uuid NOT NULL REFERENCES hive_sessions(id) ON DELETE CASCADE,
  dedup text NOT NULL,
  kind text NOT NULL,
  body jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(session_id, dedup)
);
CREATE INDEX hive_events_session ON hive_events(session_id, id);
CREATE TABLE hive_operations (
  id uuid PRIMARY KEY,
  session_id uuid NOT NULL REFERENCES hive_sessions(id) ON DELETE CASCADE,
  generation integer NOT NULL,
  lease uuid,
  status text NOT NULL CHECK(status IN ('pending','dispatched','completed','failed','uncertain','cancelled')),
  request jsonb NOT NULL,
  result jsonb,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX hive_operations_session ON hive_operations(session_id, created_at);
CREATE TABLE hive_programs (
  session_id uuid NOT NULL REFERENCES hive_sessions(id) ON DELETE CASCADE,
  id uuid NOT NULL,
  revision integer NOT NULL CHECK(revision > 0),
  definition jsonb NOT NULL,
  status text NOT NULL CHECK(status IN ('active','paused','removed')),
  PRIMARY KEY(session_id,id)
);
CREATE TABLE hive_purchases (
  id uuid PRIMARY KEY,
  account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  checkout_id text UNIQUE,
  payment_id text UNIQUE,
  pack jsonb NOT NULL,
  paid boolean NOT NULL DEFAULT false,
  reversed bigint NOT NULL DEFAULT 0 CHECK(reversed >= 0),
  created_at timestamptz NOT NULL DEFAULT now()
);
