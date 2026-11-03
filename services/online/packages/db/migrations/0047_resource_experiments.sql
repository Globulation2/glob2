-- Display metadata comes from validated embedded resource catalogs.
ALTER TABLE map_versions
  ADD COLUMN resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN required_resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
ALTER TABLE map_uploads
  ADD COLUMN resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN required_resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
ALTER TABLE generated_maps
  ADD COLUMN resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN required_resource_experiments jsonb NOT NULL DEFAULT '[]'::jsonb;
