-- Ratings (M5) and quick match (M6): rating application state and history,
-- match proposals with an accept step, ticket opt-outs and decline cooldowns.
-- See docs/multiplayer/ratings-and-matchmaking.md.

-- ------------------------------------------------------------- matches

-- Whether this match's result has been applied to ratings. Exactly one
-- transaction moves a match out of 'pending', so a re-delivered verdict can
-- never apply a rating change twice.
--   pending    waiting for a verified result
--   applied    ratings changed (rating_history holds the per-entity change)
--   unchanged  rated match, but the result does not change ratings
--              (unresolved, mutual leave, diverged or unverifiable)
--   not_rated  unrated match (rooms, casual queues)
ALTER TABLE matches
  ADD COLUMN rating_status text NOT NULL DEFAULT 'pending'
    CHECK (rating_status IN ('pending', 'applied', 'unchanged', 'not_rated')),
  ADD COLUMN rating_note text,
  ADD COLUMN ratings_applied_at timestamptz;

CREATE INDEX matches_rating_pending_idx ON matches (created_at)
  WHERE rating_status = 'pending' AND verification <> 'pending';

-- One rating change per entity per match: the audit trail behind
-- match_participants.rating_before/after and the profile rating graph.
CREATE TABLE rating_history (
  match_id uuid NOT NULL REFERENCES matches (id) ON DELETE CASCADE,
  entity_id uuid NOT NULL REFERENCES rating_entities (id) ON DELETE CASCADE,
  ladder text NOT NULL,
  result text NOT NULL CHECK (result IN ('won', 'lost')),
  mu_before double precision NOT NULL,
  sigma_before double precision NOT NULL CHECK (sigma_before > 0),
  mu_after double precision NOT NULL,
  sigma_after double precision NOT NULL CHECK (sigma_after > 0),
  display_before double precision NOT NULL,
  display_after double precision NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (match_id, entity_id)
);
CREATE INDEX rating_history_entity_idx ON rating_history (entity_id, ladder, created_at);

-- Where a rating's starting point came from: NULL for the default prior, or a
-- note such as 'docs/ai/ratings.md elo 1873' for seeded AI entities.
ALTER TABLE ratings ADD COLUMN seed_source text;

-- ------------------------------------------------------------ proposals

-- A group the matchmaker formed. Ranked all-human groups wait for every human
-- to accept ('pending'); casual and AI-backfilled groups go straight to
-- 'starting'. 'starting' proposals are handed to the MatchStarter, which must
-- be idempotent per proposal id (a new leader retries them after failover).
CREATE TABLE match_proposals (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  queue_id text NOT NULL,
  sim_version sim_version_key NOT NULL,
  -- Relay region minimising the worst member round trip; NULL when no member
  -- reported any probe (any region will do).
  region text,
  rated boolean NOT NULL,
  backfilled boolean NOT NULL DEFAULT false,
  status text NOT NULL
    CHECK (status IN ('pending', 'starting', 'started', 'cancelled', 'failed')),
  -- Map pool entry: a protocol GeneratorDescriptor without its seed.
  map jsonb NOT NULL,
  expires_at timestamptz,
  match_id uuid REFERENCES matches (id) ON DELETE SET NULL,
  start_attempts integer NOT NULL DEFAULT 0,
  failure text,
  created_at timestamptz NOT NULL DEFAULT now(),
  resolved_at timestamptz,
  -- Only proposals waiting for accepts have a deadline.
  CHECK (status <> 'pending' OR expires_at IS NOT NULL)
);
CREATE INDEX match_proposals_open_idx ON match_proposals (status, created_at)
  WHERE status IN ('pending', 'starting');

CREATE TABLE match_proposal_seats (
  proposal_id uuid NOT NULL REFERENCES match_proposals (id) ON DELETE CASCADE,
  slot smallint NOT NULL CHECK (slot BETWEEN 0 AND 11),
  -- Side (alliance) the seat plays on; the starter maps it to MatchSetup
  -- teams[].alliance.
  side smallint NOT NULL CHECK (side BETWEEN 0 AND 11),
  kind text NOT NULL CHECK (kind IN ('human', 'ai')),
  ticket_id uuid,
  account_id uuid REFERENCES accounts (id) ON DELETE CASCADE,
  ai_id text,
  rating_entity_id uuid REFERENCES rating_entities (id) ON DELETE SET NULL,
  -- Rating snapshot the group was formed with.
  mu double precision NOT NULL,
  sigma double precision NOT NULL CHECK (sigma > 0),
  response text NOT NULL DEFAULT 'pending'
    CHECK (response IN ('pending', 'accepted', 'declined', 'timeout', 'not_required')),
  responded_at timestamptz,
  PRIMARY KEY (proposal_id, slot),
  CHECK (
    (kind = 'human' AND account_id IS NOT NULL AND ticket_id IS NOT NULL AND ai_id IS NULL)
    OR (kind = 'ai' AND account_id IS NULL AND ticket_id IS NULL AND ai_id IS NOT NULL
        AND response = 'not_required')
  )
);
CREATE INDEX match_proposal_seats_account_idx ON match_proposal_seats (account_id);

-- --------------------------------------------------------------- tickets

-- 'proposed': held by a match proposal; 'declined': removed after declining
-- or not answering a ranked accept prompt.
ALTER TABLE queue_tickets DROP CONSTRAINT queue_tickets_status_check;
ALTER TABLE queue_tickets ADD CONSTRAINT queue_tickets_status_check
  CHECK (status IN ('waiting', 'proposed', 'matched', 'cancelled', 'declined', 'expired'));

ALTER TABLE queue_tickets
  -- "Allow an AI opponent": the ticket may be backfilled with AI seats.
  ADD COLUMN allow_ai_opponent boolean NOT NULL DEFAULT true,
  ADD COLUMN proposal_id uuid REFERENCES match_proposals (id) ON DELETE SET NULL;

-- An account holds at most one active ticket (waiting or held by a proposal).
DROP INDEX queue_tickets_one_waiting_idx;
CREATE UNIQUE INDEX queue_tickets_one_active_idx ON queue_tickets (account_id)
  WHERE status IN ('waiting', 'proposed');

ALTER TABLE match_proposal_seats ADD CONSTRAINT match_proposal_seats_ticket_fk
  FOREIGN KEY (ticket_id) REFERENCES queue_tickets (id) ON DELETE CASCADE;

-- Short queue bans after declining a ranked match.
CREATE TABLE queue_cooldowns (
  account_id uuid PRIMARY KEY REFERENCES accounts (id) ON DELETE CASCADE,
  until timestamptz NOT NULL,
  reason text NOT NULL CHECK (reason IN ('declined', 'timeout')),
  created_at timestamptz NOT NULL DEFAULT now()
);

-- Links a queue match to the proposal that produced it, so a MatchStarter
-- retried after failover can find the match it already created.
ALTER TABLE matches ADD COLUMN proposal_id uuid UNIQUE
  REFERENCES match_proposals (id) ON DELETE SET NULL;
