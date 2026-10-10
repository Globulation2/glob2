-- Unit snapshots are engine-validated map state, carried unchanged into match setups.
ALTER TABLE map_versions ADD COLUMN unit_catalog jsonb,
  ADD COLUMN required_unit_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
ALTER TABLE map_uploads ADD COLUMN unit_catalog jsonb,
  ADD COLUMN required_unit_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
ALTER TABLE generated_maps ADD COLUMN unit_catalog jsonb,
  ADD COLUMN required_unit_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
