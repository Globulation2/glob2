-- Working designs are mutable; match snapshots remain immutable.
ALTER TABLE colony_skins ADD COLUMN archived_at timestamptz;
CREATE TABLE colony_skin_designs (
  skin_id uuid PRIMARY KEY REFERENCES colony_skins(id),
  revision uuid NOT NULL DEFAULT gen_random_uuid(),
  applied_revision uuid,
  applied_version_id uuid REFERENCES colony_skin_versions(id),
  building_color integer NOT NULL CHECK (building_color BETWEEN 0 AND 16777215),
  swarm_mesh text NOT NULL,
  swarm_view_angle integer NOT NULL CHECK (swarm_view_angle BETWEEN 0 AND 359),
  image bytea NOT NULL CHECK (octet_length(image) BETWEEN 1 AND 1048576),
  material bytea NOT NULL CHECK (octet_length(material) BETWEEN 1 AND 262144),
  updated_at timestamptz NOT NULL DEFAULT now()
);
-- Preserve the one old working canvas, including never-applied work.
DO $$
DECLARE draft record; design_id uuid;
BEGIN
  FOR draft IN SELECT * FROM colony_skin_drafts LOOP
    design_id := draft.skin_id;
    IF design_id IS NULL THEN
      INSERT INTO colony_skins (owner_account_id, kind, name, entitlement)
      VALUES (draft.account_id, 'custom', draft.name, 'skins:designer') RETURNING id INTO design_id;
    END IF;
    INSERT INTO colony_skin_designs (skin_id, revision, building_color, swarm_mesh, swarm_view_angle, image, material, updated_at)
    VALUES (design_id, draft.revision, draft.building_color, draft.swarm_mesh, draft.swarm_view_angle, draft.image, draft.material, draft.updated_at);
    UPDATE colony_skins SET name = draft.name WHERE id = design_id;
  END LOOP;
END $$;
