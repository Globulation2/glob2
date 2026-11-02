-- Engine agents (M5/M6): the warm map pool for quick-match queues, and the
-- aggregate views over verified match history (plan section G). Verified
-- statistics and timelines land in match_team_stats and the record, replay and
-- result blobs in match_artifacts (both from 0001); see
-- docs/multiplayer/architecture.md, "Engine agents".

-- ------------------------------------------------------------ warm maps

-- Pre-generated maps per queue, map pool entry and sim version, so a match
-- start never waits for generation. The worker leader keeps a target number
-- 'ready' (or 'generating') per entry; match starters take one with
-- takeWarmMap(). Rows are created with the generate-map job they wait for.
CREATE TABLE warm_maps (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  queue_id text NOT NULL,
  sim_version sim_version_key NOT NULL,
  -- SHA-256 of the pool entry's canonical JSON (generator without its seed),
  -- so an entry changed in instance.yaml gets fresh maps.
  entry_key sha256_hex NOT NULL,
  -- The full protocol GeneratorDescriptor (with seed) the map was made from.
  generator jsonb NOT NULL,
  status text NOT NULL DEFAULT 'generating'
    CHECK (status IN ('generating', 'ready', 'taken', 'failed')),
  -- The generate-map job; recorded before the job is submitted, so no FK.
  job_id uuid UNIQUE,
  map_hash sha256_hex,
  -- The generate-map result (size, dimensions, team count, chosen seed).
  map_facts jsonb,
  failure text,
  match_id uuid REFERENCES matches (id) ON DELETE SET NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  ready_at timestamptz,
  taken_at timestamptz,
  CHECK (status NOT IN ('ready', 'taken') OR map_hash IS NOT NULL)
);
CREATE INDEX warm_maps_ready_idx ON warm_maps (queue_id, sim_version, entry_key, ready_at)
  WHERE status = 'ready';
CREATE INDEX warm_maps_open_idx ON warm_maps (queue_id, sim_version, entry_key)
  WHERE status IN ('generating', 'ready');
CREATE INDEX warm_maps_failed_idx ON warm_maps (queue_id, sim_version, entry_key, created_at)
  WHERE status = 'failed';

-- ---------------------------------------------------------------- views

-- Aggregates cover verified, ended matches of the last 90 days ("recent games");
-- callers filter further (queue, sim version) as they need.

-- One row per seat of every verified match: the base of the aggregates.
CREATE VIEW match_results_view AS
SELECT
  m.id AS match_id,
  m.origin,
  m.queue_id,
  m.rated,
  m.sim_version,
  m.map_hash,
  m.setup #>> '{map,generator,generatorId}' AS generator_id,
  m.final_tick,
  COALESCE(m.ended_at, m.created_at) AS ended_at,
  p.seat,
  p.team,
  p.kind,
  p.account_id,
  p.ai_id,
  p.rating_entity_id,
  p.outcome,
  (p.outcome = 'won') AS won
FROM matches m
JOIN match_participants p ON p.match_id = m.id
WHERE m.verification = 'verified' AND m.status = 'ended';

-- Win rate per player over recent games, by queue, by map (content hash) and
-- by generator. A player is an account, or an AI at one sim version (AI
-- revisions are never combined). `dimension` names the grouping; `key` is the
-- queue id ('room' for room matches), map hash or generator id.
CREATE VIEW recent_win_rates_view AS
WITH recent AS (
  SELECT *, CASE WHEN kind = 'ai' THEN sim_version END AS ai_sim_version
  FROM match_results_view
  WHERE ended_at > now() - interval '90 days' AND outcome IS NOT NULL
)
SELECT
  account_id,
  ai_id,
  ai_sim_version,
  CASE
    WHEN GROUPING(queue_key) = 0 THEN 'queue'
    WHEN GROUPING(map_hash) = 0 THEN 'map'
    ELSE 'generator'
  END AS dimension,
  COALESCE(queue_key, map_hash, generator_id) AS key,
  count(*)::integer AS games,
  count(*) FILTER (WHERE won)::integer AS wins,
  round(count(*) FILTER (WHERE won)::numeric / count(*), 4)::double precision AS win_rate,
  max(ended_at) AS last_played_at
FROM (SELECT *, COALESCE(queue_id, 'room') AS queue_key FROM recent) r
GROUP BY GROUPING SETS (
  (account_id, ai_id, ai_sim_version, queue_key),
  (account_id, ai_id, ai_sim_version, map_hash),
  (account_id, ai_id, ai_sim_version, generator_id)
)
HAVING COALESCE(queue_key, map_hash, generator_id) IS NOT NULL;

-- Game length over recent games, by queue and by generator (ticks; 25 per second).
CREATE VIEW recent_game_lengths_view AS
WITH recent AS (
  SELECT id, COALESCE(queue_id, 'room') AS queue_key,
         setup #>> '{map,generator,generatorId}' AS generator_id, final_tick
  FROM matches
  WHERE verification = 'verified' AND status = 'ended' AND final_tick IS NOT NULL
    AND COALESCE(ended_at, created_at) > now() - interval '90 days'
)
SELECT
  CASE WHEN GROUPING(queue_key) = 0 THEN 'queue' ELSE 'generator' END AS dimension,
  COALESCE(queue_key, generator_id) AS key,
  count(*)::integer AS games,
  avg(final_tick)::double precision AS mean_ticks,
  percentile_cont(0.5) WITHIN GROUP (ORDER BY final_tick)::double precision AS median_ticks,
  percentile_cont(0.9) WITHIN GROUP (ORDER BY final_tick)::double precision AS p90_ticks,
  max(final_tick) AS max_ticks
FROM recent
GROUP BY GROUPING SETS ((queue_key), (generator_id))
HAVING COALESCE(queue_key, generator_id) IS NOT NULL;

-- Team timelines (one row per 512-tick sample) from match_team_stats.
CREATE VIEW team_timeline_view AS
SELECT
  s.match_id,
  s.team,
  (point.value ->> 'tick')::integer AS tick,
  (point.value ->> 'units')::integer AS units,
  (point.value ->> 'buildings')::integer AS buildings,
  (point.value ->> 'prestige')::integer AS prestige,
  (point.value ->> 'hp')::integer AS hp,
  (point.value ->> 'attack')::integer AS attack,
  (point.value ->> 'defense')::integer AS defense
FROM match_team_stats s
CROSS JOIN LATERAL jsonb_array_elements(
  CASE WHEN jsonb_typeof(s.timeline) = 'array' THEN s.timeline ELSE '[]'::jsonb END
) AS point(value);

-- A human player's economy curve in each recent verified match, next to their
-- own average at the same tick over all their recent verified matches.
CREATE VIEW account_economy_curves_view AS
WITH points AS (
  SELECT r.account_id, r.match_id, r.queue_id, r.ended_at, t.tick, t.units, t.buildings, t.prestige
  FROM match_results_view r
  JOIN team_timeline_view t ON t.match_id = r.match_id AND t.team = r.team
  WHERE r.kind = 'human' AND r.account_id IS NOT NULL
    AND r.ended_at > now() - interval '90 days'
)
SELECT
  account_id,
  match_id,
  queue_id,
  ended_at,
  tick,
  units,
  buildings,
  prestige,
  (avg(units) OVER w)::double precision AS average_units,
  (avg(buildings) OVER w)::double precision AS average_buildings,
  (avg(prestige) OVER w)::double precision AS average_prestige,
  (count(*) OVER w)::integer AS games_at_tick
FROM points
WINDOW w AS (PARTITION BY account_id, tick);
