-- Frozen building catalogs travel with maps and match setups. Engine routing
-- remains keyed by sim_version; AI ratings use a separate rules identity.
ALTER TABLE map_versions ADD COLUMN building_catalog jsonb;
ALTER TABLE map_uploads ADD COLUMN building_catalog jsonb;
ALTER TABLE generated_maps ADD COLUMN building_catalog jsonb;
ALTER TABLE engine_agents ADD COLUMN building_catalog_hash sha256_hex;
ALTER TABLE matches ADD COLUMN rules_identity sim_version_key;
UPDATE matches SET rules_identity = sim_version;
CREATE INDEX matches_rules_identity_idx ON matches (rules_identity);

-- Existing AI history aggregates group by this view's version. Human history
-- continues to report the actual executable identity; AI history uses rules.
CREATE OR REPLACE VIEW match_results_view AS
SELECT m.id AS match_id, m.origin, m.queue_id, m.rated,
  (CASE WHEN p.kind = 'ai' THEN COALESCE(m.rules_identity, m.sim_version)
        ELSE m.sim_version END)::sim_version_key AS sim_version,
  m.map_hash, m.setup #>> '{map,generator,generatorId}' AS generator_id,
  m.final_tick, COALESCE(m.ended_at, m.created_at) AS ended_at,
  p.seat, p.team, p.kind, p.account_id, p.ai_id, p.rating_entity_id,
  p.outcome, (p.outcome = 'won') AS won
FROM matches m JOIN match_participants p ON p.match_id = m.id
WHERE m.verification = 'verified' AND m.status = 'ended';
