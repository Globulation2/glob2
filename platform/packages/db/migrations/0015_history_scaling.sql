-- History reads that stay proportional to what they show (the public match
-- page, match lists, the matchmaker's typical wait), instead of to all stored
-- history. Numbered 0015: 0010-0014 are left to parallel work.

-- --------------------------------------------------------- engine jobs

-- The match a verify-match job checks, as a real column: looked up by the
-- match page and by intake, which before scanned engine_jobs on
-- payload->>'matchId'. Set by submitEngineJob from the payload.
ALTER TABLE engine_jobs ADD COLUMN match_id uuid REFERENCES matches (id) ON DELETE SET NULL;
UPDATE engine_jobs j
SET match_id = (j.payload ->> 'matchId')::uuid
WHERE j.payload ->> 'matchId' ~ '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
  AND EXISTS (SELECT 1 FROM matches m WHERE m.id = (j.payload ->> 'matchId')::uuid);
CREATE INDEX engine_jobs_match_idx ON engine_jobs (match_id, kind, completed_at DESC)
  WHERE match_id IS NOT NULL;

-- ------------------------------------------------------------- matches

-- Match lists sort newest first by "ended, else started, else created"
-- (apps/api history/summaries.ts MATCH_TIME) with the id as tie-breaker.
CREATE INDEX matches_time_idx
  ON matches ((COALESCE(ended_at, started_at, created_at)) DESC, id DESC);

-- The matchmaker's typical wait (matched tickets of the last day, per queue).
CREATE INDEX queue_tickets_matched_idx ON queue_tickets (queue_id, updated_at)
  WHERE status = 'matched';

-- --------------------------------------------------------- economy curves

-- A match's economy curves: each human player's units, buildings and prestige
-- per timeline sample, next to that player's own average at the same tick over
-- their recent (90-day) verified matches. Same rows as
-- account_economy_curves_view filtered to one match, but evaluated only over
-- the match's own players' history: the view's window partitions every
-- player's timeline, and a filter on match_id cannot be pushed below it.
-- p_account limits the result to one player.
CREATE FUNCTION match_economy_curves(p_match uuid, p_account uuid DEFAULT NULL)
RETURNS TABLE (
  account_id uuid,
  seat smallint,
  tick integer,
  units integer,
  buildings integer,
  prestige integer,
  average_units double precision,
  average_buildings double precision,
  average_prestige double precision,
  games_at_tick integer
)
LANGUAGE sql STABLE AS $$
  WITH players AS (
    SELECT p.account_id, min(p.seat) AS seat
    FROM match_participants p
    WHERE p.match_id = p_match AND p.kind = 'human' AND p.account_id IS NOT NULL
      AND (p_account IS NULL OR p.account_id = p_account)
    GROUP BY p.account_id
  ),
  recent AS (
    SELECT r.account_id, r.match_id, r.team
    FROM players pl
    JOIN match_results_view r ON r.account_id = pl.account_id
    WHERE r.kind = 'human' AND r.ended_at > now() - interval '90 days'
  ),
  points AS (
    SELECT recent.account_id, recent.match_id, t.tick, t.units, t.buildings, t.prestige
    FROM recent
    JOIN team_timeline_view t ON t.match_id = recent.match_id AND t.team = recent.team
  ),
  averages AS (
    SELECT points.account_id, points.tick,
           avg(points.units)::double precision AS average_units,
           avg(points.buildings)::double precision AS average_buildings,
           avg(points.prestige)::double precision AS average_prestige,
           count(*)::integer AS games_at_tick
    FROM points
    GROUP BY points.account_id, points.tick
  )
  SELECT pt.account_id, pl.seat, pt.tick, pt.units, pt.buildings, pt.prestige,
         a.average_units, a.average_buildings, a.average_prestige, a.games_at_tick
  FROM points pt
  JOIN players pl ON pl.account_id = pt.account_id
  JOIN averages a ON a.account_id = pt.account_id AND a.tick = pt.tick
  WHERE pt.match_id = p_match
  ORDER BY pl.seat, pt.tick
$$;
