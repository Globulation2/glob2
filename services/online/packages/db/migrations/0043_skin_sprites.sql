-- Presentation derivatives do not rewrite immutable skin content.
CREATE TABLE skin_render_revisions (
  revision sha256_hex PRIMARY KEY,
  created_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE colony_skin_sprites (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  version_id uuid NOT NULL REFERENCES colony_skin_versions(id),
  render_revision sha256_hex NOT NULL REFERENCES skin_render_revisions(revision),
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','ready','failed')),
  manifest_sha256 sha256_hex REFERENCES blobs(sha256),
  error text,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(version_id,render_revision),
  UNIQUE(id,version_id),
  CHECK ((status = 'ready') = (manifest_sha256 IS NOT NULL))
);
CREATE TABLE colony_skin_sprite_pages (
  sprites_id uuid NOT NULL REFERENCES colony_skin_sprites(id),
  sha256 sha256_hex NOT NULL REFERENCES blobs(sha256),
  PRIMARY KEY(sprites_id,sha256)
);
ALTER TABLE match_colony_skins ADD COLUMN sprites_id uuid;
ALTER TABLE match_colony_skins ADD FOREIGN KEY(sprites_id,version_id)
  REFERENCES colony_skin_sprites(id,version_id);
-- Ready derivative identity must remain stable for matches which pinned it.
CREATE FUNCTION colony_skin_sprites_immutable() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF OLD.status = 'ready' THEN RAISE EXCEPTION 'ready skin sprites are immutable'; END IF;
  IF TG_OP = 'DELETE' THEN RETURN OLD; ELSE RETURN NEW; END IF;
END;
$$;
CREATE TRIGGER colony_skin_sprites_immutable BEFORE UPDATE OR DELETE ON colony_skin_sprites
  FOR EACH ROW EXECUTE FUNCTION colony_skin_sprites_immutable();

CREATE FUNCTION colony_skin_sprite_pages_immutable() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF (TG_OP <> 'INSERT' AND EXISTS(SELECT 1 FROM colony_skin_sprites WHERE id = OLD.sprites_id AND status = 'ready'))
     OR (TG_OP <> 'DELETE' AND EXISTS(SELECT 1 FROM colony_skin_sprites WHERE id = NEW.sprites_id AND status = 'ready')) THEN
    RAISE EXCEPTION 'ready skin sprite pages are immutable';
  END IF;
  IF TG_OP = 'DELETE' THEN RETURN OLD; ELSE RETURN NEW; END IF;
END;
$$;
CREATE TRIGGER colony_skin_sprite_pages_immutable BEFORE INSERT OR UPDATE OR DELETE ON colony_skin_sprite_pages
  FOR EACH ROW EXECUTE FUNCTION colony_skin_sprite_pages_immutable();
