-- Colony skins v2: a 512x512 colour atlas plus a 512x512 material-id map, one
-- 256x256 quadrant per model (worker, warrior, explorer, swarm). Layout
-- colony-v1 was never used in production, so its custom data is discarded
-- rather than converted. Purchases and entitlements are kept; preset skins
-- keep their rows and their versions are reseeded by the API on start.
DROP TRIGGER colony_skin_version_immutable ON colony_skin_versions;

DELETE FROM match_colony_skins;
DELETE FROM colony_skin_equipment;
DELETE FROM colony_skin_reports;
DELETE FROM colony_skin_drafts;
DELETE FROM colony_skin_versions;
DELETE FROM colony_skins WHERE kind = 'custom';

ALTER TABLE colony_skin_versions DROP CONSTRAINT colony_skin_versions_layout_check;
ALTER TABLE colony_skin_versions ADD CONSTRAINT colony_skin_versions_layout_check
  CHECK (layout = 'colony-v2');
ALTER TABLE colony_skin_versions
  ADD COLUMN material_sha256 sha256_hex NOT NULL REFERENCES blobs(sha256);
-- Replace 0034's content key: the same paint with another material map or swarm
-- mesh is a distinct version.
ALTER TABLE colony_skin_versions DROP CONSTRAINT colony_skin_versions_content_key;
ALTER TABLE colony_skin_versions ADD CONSTRAINT colony_skin_versions_content_key
  UNIQUE (skin_id, texture_sha256, material_sha256, layout, building_color, swarm_mesh);

ALTER TABLE colony_skin_drafts ADD COLUMN material bytea NOT NULL
  CHECK (octet_length(material) BETWEEN 1 AND 262144);
ALTER TABLE colony_skin_drafts DROP CONSTRAINT colony_skin_drafts_image_check;
ALTER TABLE colony_skin_drafts ADD CONSTRAINT colony_skin_drafts_image_check
  CHECK (octet_length(image) BETWEEN 1 AND 1048576);

CREATE TRIGGER colony_skin_version_immutable BEFORE UPDATE OR DELETE ON colony_skin_versions
  FOR EACH ROW EXECUTE FUNCTION colony_skin_version_immutable();
