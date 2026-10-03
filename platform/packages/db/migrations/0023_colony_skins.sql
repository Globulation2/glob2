-- A product/creator identity is separate from its immutable published content.
CREATE TABLE colony_skins (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  owner_account_id uuid REFERENCES accounts(id),
  kind text NOT NULL CHECK (kind IN ('preset', 'custom')),
  name text NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
  entitlement text NOT NULL CHECK (char_length(entitlement) BETWEEN 1 AND 64),
  disabled_at timestamptz,
  created_at timestamptz NOT NULL DEFAULT now(),
  CHECK ((kind = 'custom') = (owner_account_id IS NOT NULL))
);
CREATE INDEX colony_skins_owner_idx ON colony_skins(owner_account_id);

CREATE TABLE colony_skin_versions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  skin_id uuid NOT NULL REFERENCES colony_skins(id),
  texture_sha256 sha256_hex NOT NULL REFERENCES blobs(sha256),
  -- Client models are shipped/versioned separately from user paint.
  layout text NOT NULL CHECK (layout = 'colony-v1'),
  building_color integer NOT NULL CHECK (building_color BETWEEN 0 AND 16777215),
  manifest_sha256 sha256_hex NOT NULL UNIQUE,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (skin_id, texture_sha256, layout, building_color)
);
CREATE INDEX colony_skin_versions_skin_idx ON colony_skin_versions(skin_id);

-- Published content cannot be edited out from under a running match/replay.
-- Moderation disables the parent skin; it never rewrites old paint or hashes.
CREATE FUNCTION colony_skin_version_immutable() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE EXCEPTION 'published colony skin versions are immutable';
END;
$$;
CREATE TRIGGER colony_skin_version_immutable BEFORE UPDATE OR DELETE ON colony_skin_versions
  FOR EACH ROW EXECUTE FUNCTION colony_skin_version_immutable();

CREATE TABLE colony_skin_equipment (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  version_id uuid NOT NULL REFERENCES colony_skin_versions(id),
  updated_at timestamptz NOT NULL DEFAULT now()
);

-- A frozen per-team public appearance, never a player's private join ticket.
CREATE TABLE match_colony_skins (
  match_id uuid NOT NULL REFERENCES matches(id),
  team_index integer NOT NULL CHECK (team_index BETWEEN 0 AND 31),
  account_id uuid NOT NULL REFERENCES accounts(id),
  version_id uuid NOT NULL REFERENCES colony_skin_versions(id),
  assertion text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (match_id, team_index)
);
