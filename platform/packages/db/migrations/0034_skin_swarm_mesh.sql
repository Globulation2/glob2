-- The swarm mesh a design is painted for. Paint is laid out per mesh, so the
-- mesh belongs to the immutable version; 'classic' is the original swarm, which
-- every version published before mesh choice uses. The API validates ids
-- against the catalog; the database bounds their shape.
ALTER TABLE colony_skin_versions
  ADD COLUMN swarm_mesh text NOT NULL DEFAULT 'classic' CHECK (swarm_mesh ~ '^[a-z]{1,32}$');
ALTER TABLE colony_skin_drafts
  ADD COLUMN swarm_mesh text NOT NULL DEFAULT 'classic' CHECK (swarm_mesh ~ '^[a-z]{1,32}$');

-- The same paint on another mesh is a distinct version.
DO $$
DECLARE previous text;
BEGIN
  SELECT c.conname INTO STRICT previous
  FROM pg_constraint c
  WHERE c.conrelid = 'colony_skin_versions'::regclass AND c.contype = 'u'
    AND (SELECT array_agg(a.attname::text ORDER BY a.attname)
         FROM pg_attribute a
         WHERE a.attrelid = c.conrelid AND a.attnum = ANY (c.conkey))
        = ARRAY['building_color', 'layout', 'skin_id', 'texture_sha256'];
  EXECUTE format('ALTER TABLE colony_skin_versions DROP CONSTRAINT %I', previous);
END $$;
ALTER TABLE colony_skin_versions
  ADD CONSTRAINT colony_skin_versions_content_key
  UNIQUE (skin_id, texture_sha256, layout, building_color, swarm_mesh);
