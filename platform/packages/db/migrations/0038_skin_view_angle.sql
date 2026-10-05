-- Camera azimuth is cosmetic immutable version content. Legacy views remain 0.
ALTER TABLE colony_skin_versions ADD COLUMN swarm_view_angle integer NOT NULL DEFAULT 0
  CHECK (swarm_view_angle BETWEEN 0 AND 359);
ALTER TABLE colony_skin_drafts ADD COLUMN swarm_view_angle integer NOT NULL DEFAULT 0
  CHECK (swarm_view_angle BETWEEN 0 AND 359);
ALTER TABLE colony_skin_versions DROP CONSTRAINT colony_skin_versions_content_key;
ALTER TABLE colony_skin_versions ADD CONSTRAINT colony_skin_versions_content_key
  UNIQUE (skin_id, texture_sha256, material_sha256, layout, building_color, swarm_mesh, swarm_view_angle);
