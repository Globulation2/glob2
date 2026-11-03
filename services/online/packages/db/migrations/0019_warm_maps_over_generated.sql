-- The warm map pool becomes a layer over generated_maps instead of a second
-- map-generation cache. A warm map is now a pre-requested generated map (one
-- generate-map job per descriptor, through the same submission and result
-- path as rooms and on-demand queue starts); warm_maps keeps only the pool
-- bookkeeping: which queue entry the map is for and when it was taken.
-- Generation state, job, map hash, facts and failure are read from
-- generated_maps (packages/play warmMaps.ts).
--
-- Upgrade: untaken pool rows (generating, ready, failed) are dropped; the next
-- refill (every 10 s) generates replacements through generated_maps. Jobs of
-- dropped rows still complete harmlessly; their blobs are collected as
-- unreferenced. Taken rows stay, for the demand window and retention.

DROP INDEX IF EXISTS warm_maps_ready_idx;
DROP INDEX IF EXISTS warm_maps_open_idx;
DROP INDEX IF EXISTS warm_maps_failed_idx;
DROP INDEX IF EXISTS warm_maps_map_hash_idx;

DELETE FROM warm_maps WHERE status <> 'taken';

ALTER TABLE warm_maps
  DROP COLUMN status,
  DROP COLUMN generator,
  DROP COLUMN job_id,
  DROP COLUMN map_hash,
  DROP COLUMN map_facts,
  DROP COLUMN failure,
  DROP COLUMN ready_at,
  -- The generated map (with sim_version); NULL only on rows taken before 0019.
  ADD COLUMN descriptor_hash sha256_hex,
  ADD CONSTRAINT warm_maps_generated_fkey FOREIGN KEY (descriptor_hash, sim_version)
    REFERENCES generated_maps (descriptor_hash, sim_version) ON DELETE CASCADE,
  ADD CONSTRAINT warm_maps_pooled_check CHECK (taken_at IS NOT NULL OR descriptor_hash IS NOT NULL);

-- Untaken maps per queue entry (refill counts, takeWarmMap).
CREATE INDEX warm_maps_pool_idx ON warm_maps (queue_id, sim_version, entry_key, created_at)
  WHERE taken_at IS NULL;
-- Recently taken maps per entry (demand-driven refill target).
CREATE INDEX warm_maps_taken_idx ON warm_maps (queue_id, sim_version, entry_key, taken_at)
  WHERE taken_at IS NOT NULL;
CREATE INDEX warm_maps_generated_idx ON warm_maps (descriptor_hash, sim_version);
