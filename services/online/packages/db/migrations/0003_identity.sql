-- Identity flows (M3): rename limits, unique registered names, browser
-- handoff state, web sessions and provider sign-in flows.

-- Registered accounts may rename once per configured interval; guests get
-- generated names. NULL: never renamed (the first rename is always allowed).
ALTER TABLE accounts ADD COLUMN display_name_changed_at timestamptz;

-- Display names of registered accounts are unique per instance, ignoring case.
-- Guests (generated Guest-NNNN names) and deleted accounts are exempt.
CREATE UNIQUE INDEX accounts_registered_display_name_key ON accounts (lower(display_name))
  WHERE kind = 'registered' AND status <> 'deleted';

-- Handoff attempts: whether the identity is linked to the requesting account or
-- the socket switches accounts; the browser the attempt is bound to (hash of a
-- cookie set on the first visit of /signin); the account an identity conflict
-- named; and when the waiting socket received the result (exactly once). The
-- resume secret (hashed) lets the client re-attach from a new socket.
ALTER TABLE signin_attempts
  ADD COLUMN resume_hash sha256_hex,
  ADD COLUMN mode text NOT NULL DEFAULT 'signin' CHECK (mode IN ('link', 'signin')),
  ADD COLUMN client_platform text NOT NULL DEFAULT 'desktop'
    CHECK (client_platform IN ('desktop', 'android', 'ios', 'browser')),
  ADD COLUMN browser_binding_hash sha256_hex,
  ADD COLUMN conflict_account_id uuid REFERENCES accounts (id) ON DELETE CASCADE,
  ADD COLUMN failure_reason text
    CHECK (failure_reason IN ('expired', 'denied', 'cancelled', 'conflict', 'error')),
  ADD COLUMN linked boolean,
  ADD COLUMN delivered_at timestamptz;

-- Browser sessions of the web app (/signin, admin pages later). The cookie holds
-- a random secret; only its SHA-256 is stored.
CREATE TABLE web_sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  token_hash sha256_hex NOT NULL UNIQUE,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  last_used_at timestamptz,
  revoked_at timestamptz
);
CREATE INDEX web_sessions_account_idx ON web_sessions (account_id);

-- An authorization-code flow in progress with an external provider, keyed by
-- the hash of its `state`. Holds the PKCE verifier and nonce until the callback
-- (which may be a cross-site form_post, so it cannot rely on cookies alone).
CREATE TABLE auth_flows (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  state_hash sha256_hex NOT NULL UNIQUE,
  provider text NOT NULL,
  code_verifier text NOT NULL,
  nonce text NOT NULL,
  purpose text NOT NULL CHECK (purpose IN ('handoff', 'web')),
  attempt_id uuid REFERENCES signin_attempts (id) ON DELETE CASCADE,
  browser_binding_hash sha256_hex,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  consumed_at timestamptz
);
CREATE INDEX auth_flows_expires_idx ON auth_flows (expires_at);
