-- Includes the all-default case: absence of appearance rows is not permission
-- to pick newly equipped cosmetics during a reconnect.
ALTER TABLE matches ADD COLUMN skins_frozen_at timestamptz;
