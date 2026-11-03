-- Resume an unpublished revision of an owned skin across devices.
ALTER TABLE colony_skin_drafts ADD COLUMN skin_id uuid REFERENCES colony_skins(id);
